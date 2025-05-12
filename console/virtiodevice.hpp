#pragma once
#include <atomic>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include <mutex> // Added for std::mutex
#include "l2cpu.h"

class VirtioDevice {
protected:
    int l2cpu_idx;
    L2CPU l2cpu;
    uint64_t starting_address;
    uint8_t* memory;
    uint8_t* mmio_base;
    uint32_t* interrupt_register;
    std::atomic<bool>& exit_thread_flag;
    std::mutex& interrupt_register_lock; // Mutex for accesses to interrupt register
    int interrupt_number;
    uint64_t mmio_region_offset;
    uint32_t num_queues = 1; // Default, can be overridden by derived class
    uint64_t queue_header_size = 0; // Default, can be overridden by derived class
    uint16_t queue_size = 1024; // Apparently largest value is 16384?

    uint32_t *magic_value;
    uint32_t *version;
    uint32_t *device_id;
    uint32_t *device_features;
    uint32_t *device_features_sel;
    uint32_t *queue_num_max;
    uint32_t *queue_ready;
    uint32_t *queue_notify;
    uint32_t *interrupt_status;
    uint32_t *interrupt_ack;
    uint32_t *status;
    uint32_t *queue_desc_low;
    uint32_t *queue_desc_high;
    uint32_t *queue_avail_low;
    uint32_t *queue_avail_high;
    uint32_t *queue_used_low;
    uint32_t *queue_used_high;
    uint32_t *queue_select;

    uint32_t device_features_list[2] = {0, 0}; // Default, set in subclass constructor or setup

    std::vector<struct vring_desc*> desc;
    std::vector<struct vring_avail*> avail;
    std::vector<struct vring_used*> used;
    std::vector<uint64_t> descriptor_table_address;
    std::vector<uint64_t> available_ring_address;
    std::vector<uint64_t> used_ring_address;

    std::shared_ptr<TlbWindow2M> window, interrupt_address_window; // MMIO window as a class member

public:
    VirtioDevice(int l2cpu_idx_, std::atomic<bool>& exit_flag, std::mutex& interrupt_register_lock, int interrupt_number_, uint64_t mmio_region_offset_)
        : l2cpu_idx(l2cpu_idx_), l2cpu(l2cpu_idx_), exit_thread_flag(exit_flag), interrupt_register_lock(interrupt_register_lock), interrupt_number(interrupt_number_), mmio_region_offset(mmio_region_offset_) {

        starting_address = l2cpu.get_starting_address();
        uint64_t address = starting_address + l2cpu.get_memory_size() - mmio_region_offset;
        window = l2cpu.get_persistent_2M_tlb_window(address); // assign to class member
        mmio_base = reinterpret_cast<uint8_t*>(window->get_window());
        memory = l2cpu.get_memory_ptr();
        uint64_t interrupt_address = 0xFFFFF7FEFFF10000ULL + 0x404;
        interrupt_address_window = l2cpu.get_persistent_2M_tlb_window(interrupt_address);
        interrupt_register = reinterpret_cast<uint32_t*>(interrupt_address_window->get_window());

        memset(mmio_base, 0, 0x200); // 0->0x100 for common config, 0x100 onwards for device specific config
        magic_value = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_MAGIC_VALUE);
        version = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_VERSION);
        device_id = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_DEVICE_ID);
        device_features = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_DEVICE_FEATURES);
        device_features_sel = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_DEVICE_FEATURES_SEL);
        queue_num_max = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_QUEUE_NUM_MAX);
        queue_ready = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_QUEUE_READY);
        queue_notify = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_QUEUE_NOTIFY);
        interrupt_status = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_INTERRUPT_STATUS);
        interrupt_ack = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_INTERRUPT_ACK);
        status = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_STATUS);
        queue_desc_low = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_QUEUE_DESC_LOW);
        queue_desc_high = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_QUEUE_DESC_HIGH);
        queue_avail_low = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_QUEUE_AVAIL_LOW);
        queue_avail_high = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH);
        queue_used_low = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_QUEUE_USED_LOW);
        queue_used_high = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_QUEUE_USED_HIGH);
        queue_select = reinterpret_cast<uint32_t *>(mmio_base + VIRTIO_MMIO_QUEUE_SEL);

        *magic_value = 0x74726976;
        *version = 2;
        *queue_num_max = queue_size;
    }

    // Virtual hooks for queue processing, to be implemented by subclasses
    virtual void process_queue_start(int queue_idx, uint8_t* addr, uint64_t len) = 0;
    virtual void process_queue_data(int queue_idx, uint8_t* addr, uint64_t len) = 0;
    virtual void process_queue_complete(int queue_idx, uint8_t* addr, uint64_t len) = 0;
    virtual bool queue_has_data(int queue_idx) = 0;

    inline void ack_interrupt(){
        uint32_t interrupt_status_val = *interrupt_status;
        uint32_t interrupt_ack_val = *interrupt_ack;
        uint32_t interrupt_register_val = *interrupt_register;
        if ((interrupt_ack_val & 1) == 1) {
            *interrupt_status = ~VIRTIO_MMIO_INT_VRING & interrupt_status_val;
            *interrupt_ack = ~1 & interrupt_ack_val;
            std::lock_guard<std::mutex> guard(interrupt_register_lock);
            *interrupt_register = *interrupt_register & ~(1 << (interrupt_number - 5));
        }
    }

    inline void set_interrupt(){
        uint32_t interrupt_status_val = *interrupt_status;
        uint32_t interrupt_register_val = *interrupt_register;
        if (interrupt_status_val==0){
            *interrupt_status = VIRTIO_MMIO_INT_VRING | interrupt_status_val;
            std::lock_guard<std::mutex> guard(interrupt_register_lock);
            *interrupt_register = *interrupt_register | (1 << (interrupt_number - 5));
        }
    }

    void device_setup(){
        while (!exit_thread_flag) {
            __sync_synchronize();
            if (*status & VIRTIO_CONFIG_S_DRIVER) {
                break;
            }
        }
        while (!exit_thread_flag) {
            *device_features = device_features_list[*device_features_sel];
            __sync_synchronize();

            if (*status & VIRTIO_CONFIG_S_FEATURES_OK) {
                break;
            }
        }

        // Resize vectors for queue pointers
        desc.resize(num_queues, nullptr);
        avail.resize(num_queues, nullptr);
        used.resize(num_queues, nullptr);
        descriptor_table_address.resize(num_queues, 0);
        available_ring_address.resize(num_queues, 0);
        used_ring_address.resize(num_queues, 0);

        uint32_t prev_queue_select = -1;
        while (!exit_thread_flag) {
            __sync_synchronize();
            uint32_t queue_select_val = *queue_select;
            uint32_t queue_ready_val = *queue_ready;
            *queue_ready = 0;
            __sync_synchronize();

            if (queue_ready_val && (queue_select_val != prev_queue_select)) {
                descriptor_table_address[queue_select_val] = ((uint64_t)(*queue_desc_high) << 32) | (*queue_desc_low);
                available_ring_address[queue_select_val] = ((uint64_t)(*queue_avail_high) << 32) | (*queue_avail_low);
                used_ring_address[queue_select_val] = ((uint64_t)(*queue_used_high) << 32) | (*queue_used_low);
                prev_queue_select = queue_select_val;
                if (queue_select_val == (num_queues - 1))
                    break;
            }
            usleep(1);
        }
        for (uint32_t i = 0; i < num_queues; i++) {
            desc[i] = (struct vring_desc*) (memory + (descriptor_table_address[i] - starting_address));
            avail[i] = (struct vring_avail*) (memory + (available_ring_address[i] - starting_address));
            used[i] = (struct vring_used*) (memory + (used_ring_address[i] - starting_address));
        }
        while (!exit_thread_flag){
            __sync_synchronize();
            if (*status & VIRTIO_CONFIG_S_DRIVER_OK) {
                break;
            }
        }
    }

    void device_loop(){
        std::vector<uint16_t> idx(num_queues, 0);

        while (!exit_thread_flag) {
            if (*magic_value != 0x74726976) {
                return;
            }
            __sync_synchronize();
            // uint32_t queue_notify_val = *queue_notify;
            ack_interrupt();

            for(uint32_t i=0;i<num_queues;i++){
                struct vring_desc *desc_q = desc[i];
                struct vring_avail *avail_q = avail[i];
                struct vring_used *used_q = used[i];
                
                // if (queue_notify_val == i) {
                    __sync_synchronize();
                    uint16_t avail_idx = avail_q->idx;
                    if (idx[i] != avail_idx && queue_has_data(i)) {
                        uint16_t desc_idx = avail_q->ring[idx[i] % queue_size];
                        uint16_t desc_idx_head = desc_idx;
                        uint16_t used_idx = used_q->idx;
                        
                        uint64_t num_bytes_written = 0;
                        uint64_t l = desc_q[desc_idx % queue_size].len;
                        uint64_t a = desc_q[desc_idx % queue_size].addr;
                        uint8_t *addr = memory + (a - starting_address);;
                        
                        
                        while (true) {
                            l = desc_q[desc_idx % queue_size].len;
                            a = desc_q[desc_idx % queue_size].addr;
                            addr = memory + (a - starting_address);
                            
                            if (num_bytes_written < queue_header_size) {
                                process_queue_start(i, addr, l);
                            } else if ((desc_q[desc_idx].flags & VRING_DESC_F_NEXT)) {
                                process_queue_data(i, addr, l);
                            } else {
                                process_queue_complete(i, addr, l);
                                break;
                            }
                            num_bytes_written += l;
                            desc_idx = desc_q[desc_idx % queue_size].next;
                        }
                        
                        used_q->ring[used_idx % queue_size].id = desc_idx_head;
                        used_q->ring[used_idx % queue_size].len = num_bytes_written - queue_header_size;
                        __sync_synchronize();
                        used_q->idx = used_idx + 1;
                        __sync_synchronize();
                        set_interrupt();
                        __sync_synchronize();
                        idx[i] += 1;
                    }
                // }
            }
            usleep(1);
        }
    }
    virtual ~VirtioDevice() = default;

    
};
