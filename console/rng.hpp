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
#include <linux/virtio_ring.h>

#include "l2cpu.h"

#define PMEM_REGION_SIZE (1200ULL*1024*1024)
#define TTETH_SHM_REGION_OFFSET (PMEM_REGION_SIZE + (2ULL*1024*1024) + (2ULL*1024*1024))

#define X280_REGISTERS 0xFFFFF7FEFFF10000ULL
#define INTERRUPT_NUMBER 32

 struct shared_data{
    uint32_t magic_value;
    uint32_t version;
    uint32_t device_id;
    uint32_t vendor_id;
    
    uint32_t device_features;
    uint32_t device_features_select;

    uint32_t __padding[2];

    uint32_t driver_features;
    uint32_t driver_features_select;
    
    uint32_t ___padding[2];

    uint32_t queue_select;
    uint32_t queue_num_max;
    uint32_t queue_num;
    
    uint32_t ____padding[2];
    
    uint32_t queue_ready;

    uint32_t _____padding[2];

    uint32_t queue_notify;

    uint32_t padding0[3];

    uint32_t interrupt_status;
    uint32_t interrupt_ack;

    uint32_t padding1[2];

    uint32_t status;

    uint32_t padding2[3];

    uint32_t queue_desc_low;
    uint32_t queue_desc_high;

    uint32_t padding3[2];

    uint32_t queue_avail_low;
    uint32_t queue_avail_high;

    uint32_t padding4[2];

    uint32_t queue_used_low;
    uint32_t queue_used_high;

    uint32_t padding5[21];

    uint32_t config_generation;
};

uint32_t device_features_list[32];
uint32_t driver_features_list[32];

#define QUEUE_SIZE 16

int rng_loop(int l2cpu_idx, std::atomic<bool>& exit_thread_flag){

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

    uint64_t dram_starting_address = l2cpu.get_starting_address();
    auto dram_window = l2cpu.get_persistent_4G_tlb_window(dram_starting_address);
    uint8_t *dram = dram_window->get_window();

    memset(base, 0, sizeof(struct shared_data));
    base->magic_value = 0x74726976;
    base->version = 0x2;
    base->device_id = 4;

    /*
    This is buggy, how to properly implement this?
    We need to keep checking for every time device_features_select changes
    and accordingly update device_features to the correct value
    */
    base->device_features = 1;
    device_features_list[0] = 1<<5;
    device_features_list[1] = 1;

    // base->config.capacity = 1024;

    base->queue_num_max = QUEUE_SIZE;

    while (true){
        if (base->magic_value != 0x74726976){
            return 0;
        }
        //base->device_features = device_features_list[base->device_features_select];
        __sync_synchronize();

        if (base->queue_ready == 1) {
            break;
        }
    }
    // Initialization phase done?


    // What if we need multiple queues?
    uint64_t descriptor_table_address = ((uint64_t)base->queue_desc_high << 32) | base->queue_desc_low;
    struct vring_desc *desc = reinterpret_cast<struct vring_desc*>(dram + (descriptor_table_address - dram_starting_address));

    uint64_t available_ring_address = ((uint64_t)base->queue_avail_high << 32) | base->queue_avail_low;
    struct vring_avail *avail = reinterpret_cast<struct vring_avail*>(dram + (available_ring_address - dram_starting_address));

    uint64_t used_ring_address = ((uint64_t)base->queue_used_high << 32) | base->queue_used_low;
    struct vring_used *used = reinterpret_cast<struct vring_used*>(dram + (used_ring_address - dram_starting_address));

    uint16_t idx=0;
    while (!exit_thread_flag){
        if (base->magic_value != 0x74726976){
          return 0;
        }
        if ((base->interrupt_ack & 1) == 1){
            base->interrupt_status = ~1 & base->interrupt_status;
            base->interrupt_ack = ~1 & base->interrupt_ack;
            *interrupt_register = (*interrupt_register) & ~(1 << (INTERRUPT_NUMBER - 5));
        }
        if(base->queue_notify == 0){
            if (idx != avail->idx){
                uint16_t desc_idx = avail->ring[avail->idx % QUEUE_SIZE] % QUEUE_SIZE;
                uint32_t l = desc[desc_idx].len;
                uint64_t a = desc[desc_idx].addr;
                uint8_t *destination = reinterpret_cast<uint8_t*>(dram + (a - dram_starting_address));

                // Fill buffer with random bytes
                for(uint32_t i=0;i<desc[desc_idx].len; i++)
                    destination[i]=i;

                used->ring[(used->idx)%QUEUE_SIZE].id=desc_idx;
                used->ring[(used->idx)%QUEUE_SIZE].len=l;
                __sync_synchronize();
                used->idx +=1;
                base->interrupt_status = 1 | base->interrupt_status;
                *interrupt_register = (1 << (INTERRUPT_NUMBER - 5));
                idx +=1;
            }
        }
        usleep(1);
    }
    return 0;
}

//int main(int argc, char **argv){
//    int l2cpu=0;
//    const char* const short_opts = "l:h";
//    const option long_opts[] = {
//            {"l2cpu", required_argument, nullptr, 'l'},
//            {"help", no_argument, nullptr, 'h'},
//            {nullptr, no_argument, nullptr, 0}
//    };
//
//    while (true)
//    {
//        const auto opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);
//
//        if (-1 == opt)
//            break;
//
//        switch (opt)
//        {
//        case 'l':
//            l2cpu = std::stoi(optarg);
//            break;
//
//        case 'h': // -h or --help
//        case '?': // Unrecognized option
//        default:
//            std::cout <<
//            "--l2cpu <l>:         L2CPU to attach to\n"
//            "--help:              Show help\n";
//            exit(1);
//        }
//    }
//
//    if (l2cpu < 0 || l2cpu > 3){
//        std::cerr<<"l2cpu must be one of 0,1,2,3"<<"\n";
//        exit(1);
//    }
//
//    while (true){
//        /*
//        We call run_once in a loop
//        If the chip resets or the MAGIC value disappears,
//        we sleep for a bit and try setting everything up again
//        */
//        run_once(l2cpu);
//        std::cout<<"Sleeping for a bit and trying again"<<"\n";
//        usleep(100000);
//    }
//}
