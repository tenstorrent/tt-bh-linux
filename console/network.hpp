// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdint.h>
#include <string.h>
#include <slirp/libslirp.h>
#include <sys/time.h>
#include <sys/select.h>
#include <signal.h>
#include <inttypes.h>
#include <iostream>
#include <cassert>
#include <getopt.h>
#include <atomic>

#include "l2cpu.h"

extern "C" {
#include <slirp/libvdeslirp.h>
}

#define MTU 1500 // MTU (Ignoring ethernet header) that is set for adapter = 1500
#define PACKET_SIZE 1514 // MTU + ethernet header size = 1514

#define PMEM_REGION_SIZE (32ULL*1024*1024)
#define TTETH_SHM_REGION_OFFSET (PMEM_REGION_SIZE + (2ULL*1024*1024))

#define X280_REGISTERS 0xFFFFF7FEFFF10000ULL
#define INTERRUPT_NUMBER 33

#define NETWORK_BUFFER_SIZE 500

#define TTETH_MAGIC 0x4D13ED8246A0f856ULL

struct packet{
    uint32_t len;
    char data[PACKET_SIZE];
};

struct one_side_buffer{
    uint32_t location_sent;
    uint32_t location_rcvd;
    struct packet buffer[NETWORK_BUFFER_SIZE];
};

struct shared_data{
    uint64_t magic;
    uint32_t interrupts;
    struct one_side_buffer send;
    struct one_side_buffer recv;
};


int network_loop(int l2cpu_idx, std::atomic<bool>& exit_thread_flag){

    L2CPU l2cpu(l2cpu_idx);

    // Assumes that 2M memory region is at the region just before the beginning of th pmem, which itself is at the end of the dram
    uint64_t address = l2cpu.get_starting_address() + l2cpu.get_memory_size() - TTETH_SHM_REGION_OFFSET;

    auto window = l2cpu.get_persistent_2M_tlb_window(address);
    struct shared_data* base = reinterpret_cast<struct shared_data*>(window->get_window());

    uint64_t interrupt_address = X280_REGISTERS + 0x404; // X280 Global Interrupts Register Bits 31 to 0
    // For some reason, interrupts 6->9 don't work for this even though I think they should
    // Only interrupts 0->5 are reserved, don't know if 6->9 are also connected to something else
    // Interrupts are triggered by setting bit INTERRUPT_NUMBER-5 in the register referenced above
    assert(INTERRUPT_NUMBER >= 10 && INTERRUPT_NUMBER <= 36);
    auto interrupt_address_window = l2cpu.get_persistent_2M_tlb_window(interrupt_address);
    uint32_t* interrupt_register = reinterpret_cast<uint32_t*>(interrupt_address_window->get_window());

    memset(&(base->send), 0, sizeof(struct one_side_buffer));
    memset(&(base->recv), 0, sizeof(struct one_side_buffer));
    struct one_side_buffer* send = &(base->send);
    struct one_side_buffer* recv = &(base->recv);

    SlirpConfig slirpcfg;
    slirpcfg.if_mru = PACKET_SIZE;
    slirpcfg.if_mtu = PACKET_SIZE;
    struct vdeslirp *myslirp;
    vdeslirp_init(&slirpcfg, VDE_INIT_DEFAULT);
    myslirp = vdeslirp_open(&slirpcfg);

    // Port Forwarding for SSH
    struct in_addr host, guest;
    inet_aton("127.0.0.1", &host);
    inet_aton("10.0.2.15", &guest);
    vdeslirp_add_fwd(myslirp, 0, host, 2222+l2cpu_idx, guest, 22);

    int len, ret_code;
    int slirp_fd = vdeslirp_fd(myslirp);
    void *buf=malloc(PACKET_SIZE);
    struct packet *location;

    // Prevent writing into a closed socket (which may happen after a TCP reset) 
    // (which causes a SIGPIPE) from killing the process
    signal(SIGPIPE, SIG_IGN);
    // Unused variable, might use it for something in the future so leaving it around
    // bool irq_asserted = false;

    while (!exit_thread_flag){
        if (base->magic != TTETH_MAGIC) {
            printf("Magic was %" PRIu64 ", not %lld trying again\n", base->magic, TTETH_MAGIC);
            break;
        }
        struct timeval tv;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(slirp_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 1;

        if (base->interrupts == 1){
            *interrupt_register = (*interrupt_register) & ~(1 << (INTERRUPT_NUMBER - 5));
            // irq_asserted = false;
            base->interrupts=0;
        }
        ret_code = select(slirp_fd + 1, &rfds, NULL, NULL, &tv);
        if ((send->location_rcvd != ((send->location_sent + 1)%NETWORK_BUFFER_SIZE)) && ret_code > 0){
            len = vdeslirp_recv(myslirp, buf, PACKET_SIZE);

            uint32_t location_sent = send->location_sent;

            location = send->buffer + location_sent;
            memcpy(location->data, buf, len);
            location->len = len;

            // Memory barrier to ensure data is written before updating the pointer
            __sync_synchronize();

            send->location_sent = (location_sent + 1) % NETWORK_BUFFER_SIZE; // pointer to data
            // Set the (Interrupt_number -5)th bit in the X280_Global_Interrupts_31_0 register to trigger an interrupt
            // Something about first 6 interrupts being reserved for other uses
            if(base->interrupts==0){
                *interrupt_register = (1 << (INTERRUPT_NUMBER - 5));
                // irq_asserted = true;
            }
        }
        if (recv->location_rcvd!=recv->location_sent){
            uint32_t location_rcvd = recv->location_rcvd;

            location = recv->buffer + location_rcvd;
            len = location->len;
            memcpy(buf, location->data, len);

            // Memory barrier to ensure data is read before updating the pointer
            __sync_synchronize();

            recv->location_rcvd = (location_rcvd + 1)%NETWORK_BUFFER_SIZE;

            ret_code = vdeslirp_send(myslirp, buf, len);
            if (ret_code < 0){
                perror("vdeslirp_send failed");
                break;
            }
        }
    }

    vdeslirp_close(myslirp);
    free(buf);
    return 0;
}

