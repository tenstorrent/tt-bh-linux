// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdint.h>
#include <string.h>
#include <slirp/libslirp.h>
#include <sys/time.h>
#include <sys/select.h>
#include <signal.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <getopt.h>
#include <inttypes.h>
#include <iostream>
#include <queue>
#include <signal.h>
#include <slirp/libslirp.h>
#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <mutex> // Added for std::mutex
extern "C" {
#define class __class_compat // Rename 'class' to avoid C++ keyword conflict

#include <linux/virtio_net.h>
#include <linux/virtio_ring.h>

#ifdef class
#undef class // Undefine our temporary macro if it was defined
#endif
}

#include "l2cpu.h"
#include "virtiodevice.hpp"

extern "C" {
#include <slirp/libvdeslirp.h>
}

#define PACKET_SIZE 1514

class VirtioNet : public VirtioDevice {
public:
    SlirpConfig slirpcfg;
    struct vdeslirp *myslirp = nullptr;
    int slirp_fd = -1;
    uint8_t buffer[PACKET_SIZE];

    VirtioNet(int l2cpu_idx, std::atomic<bool>& exit_flag, std::mutex& interrupt_register_lock, int interrupt_number_, uint64_t mmio_region_offset_)
        : VirtioDevice(l2cpu_idx, exit_flag, interrupt_register_lock, interrupt_number_, mmio_region_offset_) {

        num_queues = 2;
        device_features_list[0] = 1<<VIRTIO_NET_F_GUEST_CSUM;
        device_features_list[1] = 1<<(VIRTIO_F_VERSION_1-32);
        
        // Slirp setup
        vdeslirp_init(&slirpcfg, VDE_INIT_DEFAULT);
        myslirp = vdeslirp_open(&slirpcfg);
        struct in_addr host, guest;
        inet_aton("127.0.0.1", &host);
        inet_aton("10.0.2.15", &guest);
        vdeslirp_add_fwd(myslirp, 0, host, 2222, guest, 22);
        slirp_fd = vdeslirp_fd(myslirp);
        signal(SIGPIPE, SIG_IGN);

        *device_id = VIRTIO_ID_NET;
        queue_header_size = sizeof(struct virtio_net_hdr_mrg_rxbuf);
      }

    void process_queue_start(int queue_idx, uint8_t* addr, uint64_t len) override {
        // Do nothing here
    }

    void process_queue_data(int queue_idx, uint8_t* addr, uint64_t len) override {
        // Do nothing here
    }

    void process_queue_complete(int queue_idx, uint8_t* addr, uint64_t len) override {
      /*
      For the network device, we seem to always just get one descriptor of size 1514+sizeof(struct virtio_net_hdr_mrg_rxbuf)
      So we process that in this function cause the last descriptor doesn't have the next flag set
      */
      if (queue_idx==0){
          struct virtio_net_hdr_mrg_rxbuf* hdr = reinterpret_cast<struct virtio_net_hdr_mrg_rxbuf*>(addr);
          hdr->hdr.flags = 0;
          hdr->num_buffers = 1;
          hdr->hdr.gso_type = 0;
          hdr->hdr.gso_size = 0;
          ssize_t pktlen = vdeslirp_recv(myslirp, buffer, PACKET_SIZE);
          if (pktlen > 0) {
              memcpy(addr + sizeof(struct virtio_net_hdr_mrg_rxbuf), buffer, pktlen);
          }
        } else if(queue_idx==1) {
            uint64_t payload_len = len - sizeof(struct virtio_net_hdr_mrg_rxbuf);
            memcpy(buffer, addr + sizeof(struct virtio_net_hdr_mrg_rxbuf), payload_len);
            int ret = vdeslirp_send(myslirp, buffer, payload_len);
            if (ret < 0) {
                printf("vdeslirp_send failed: %d\n", ret);
            }
        }
    }

    inline bool queue_has_data(int queue_idx){
      if(queue_idx==0){
        // Check if slirp has data for us, if so return true
        struct timeval tv;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(slirp_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        return select(slirp_fd + 1, &rfds, NULL, NULL, &tv) > 0;

      } else {
        return true;
      }
    }
};
