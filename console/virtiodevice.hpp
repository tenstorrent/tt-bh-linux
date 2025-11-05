#pragma once
#include <atomic>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include <mutex> // Added for std::mutex
#include <poll.h>
#include "l2cpu.h"

#include "virtio_msg.h"
#include "spsc_queue.h"

// First 4K bytes reserved for registers for this device in case we need it for something
struct blackhole_regs {
	uint32_t doorbell_reg_generation;
	uint32_t doorbell_reg_supported;
	uint64_t doorbell_reg_address;
	uint32_t doorbell_reg_write_value;
	uint32_t regs[1019];
};

/*
Virtual Base Class that implements most of the device-agnostic functionality needed
to emulate a the device side of a virtio-mmio device added to the L2CPU's device tree
*/
class VirtioDevice {
protected:
    int l2cpu_idx;
    L2CPU l2cpu;
    // Starting address of L2CPU's DRAM
    uint64_t starting_address;

    // Ptr to starting address of L2CPU's DRAM
    // This ptr is used to interact with the virtqueues
    uint8_t* memory;
    
    std::shared_ptr<TlbWindow2M> window, interrupt_address_window; // MMIO window as a class member

    // Ptr to virtio-mmio device's reg region
    // None of the virtuqeueus/actual data transfer happens here
    // Only used for config/negotiation
    uint8_t* mmio_base;
    uint64_t mmio_region_offset;

    // Interrupt Number specified in device tree for virtio-mmio device
    int interrupt_number;
    // Ptr to L2CPU's special interrupt register
    uint32_t* interrupt_register;
    // We need to read and write back the interrupt register while setting/clearing interrupts
    // And since we have multiple threads in parallel doing this, we need a lock for this
    std::mutex& interrupt_register_lock;
    
    std::atomic<bool>& exit_thread_flag;

    // Properties of Virtqueues used by device
    // Number of virtqueues used by device
    // Most use 1, some like network may use many
    // can be overriden by derived class
    uint32_t num_queues = 1; 
     // Size of header in descriptor table, 
     // essentially sort of a "number of bytes to skip" number
     // can be overriden by derived class
    uint64_t queue_header_size = 0;
    // Max size of virtqueue, probably should make this as large as possible, 16384 maybe?
    uint16_t queue_size = 16384;

    uint32_t device_features_list[2] = {0, 0}; // Default, set in subclass constructor or setup
    uint32_t driver_features_list[2] = {0, 0}; // Default, set in subclass constructor or setup

    // Virtqueue addresses that the driver provides the device
    std::vector<uint64_t> descriptor_table_address;
    std::vector<uint64_t> available_ring_address;
    std::vector<uint64_t> used_ring_address;
    // Pointers to the virtqueues in L2CPU memory
    std::vector<struct vring_desc*> desc;
    std::vector<struct vring_avail*> avail;
    std::vector<struct vring_used*> used;

    spsc_queue drv2dev, dev2drv;
    uint32_t device_id;
    uint32_t device_status;
    uint8_t device_config[0x100];
    uint32_t device_config_size;

    struct blackhole_regs* regs;
    int chardev_fd;

public:
    VirtioDevice(int l2cpu_idx_, std::atomic<bool>& exit_flag, std::mutex& lock, int interrupt_number_, uint64_t mmio_region_offset_)
        : l2cpu_idx(l2cpu_idx_), 
          l2cpu(l2cpu_idx_), 
          mmio_region_offset(mmio_region_offset_),
          interrupt_number(interrupt_number_),
          interrupt_register_lock(lock),
          exit_thread_flag(exit_flag) {

        starting_address = l2cpu.get_starting_address();

        uint64_t address = starting_address + l2cpu.get_memory_size() - mmio_region_offset;
        window = l2cpu.get_persistent_2M_tlb_window(address);
        mmio_base = reinterpret_cast<uint8_t*>(window->get_window());

        memory = l2cpu.get_memory_ptr();

        // TODO: Check if (interrupt_number-5) is in valid 
        // range, and adjust which register to use accordingly
        uint64_t interrupt_address = 0x2FF10000 + 0x404;
        interrupt_address_window = l2cpu.get_persistent_2M_tlb_window(interrupt_address);
        interrupt_register = reinterpret_cast<uint32_t*>(interrupt_address_window->get_window());

        memset(mmio_base, 0, 4096*3);
        regs = reinterpret_cast<struct blackhole_regs*>(mmio_base); 
        regs->doorbell_reg_generation = 0;
        spsc_open(&drv2dev, "drv2dev", mmio_base + 4096, 4096);
        spsc_open(&dev2drv, "dev2drv", mmio_base + 4096*2, 4096);

        
        chardev_fd = open("/dev/tenstorrent/0", O_RDWR);
        if (chardev_fd < 0){
          perror("chardev open failed");
          exit(1);
        }
    }

    /*
    Each queue in a device needs to implement these functions on how to actually send/recv data from the virtqueue
    Sometimes we just get one descriptor that we need to fill in with data, the size of this descriptor is normally
    header size + amount of data to fill. The logic for these kind of queues can be implemented using process_queue_start alone

    Sometimes we need to fill in data across multiple desciptors, the first descriptor is of header size, and the next n descriptors
    have the actual data filled in. The header gets processed by process_queue_start, the next n descriptors get processed by
    process_queue_data

    Sometimes the last descriptor is just of size 1 byte, normally used to store the status value of the read or write
    That can be done using process_queue_complete
    */
    virtual void process_queue_start(int queue_idx, uint8_t* addr, uint64_t len) = 0;
    virtual void process_queue_data(int queue_idx, uint8_t* addr, uint64_t len) = 0;
    virtual void process_queue_complete(int queue_idx, uint8_t* addr, uint64_t len) = 0;
    
    // Each queue in a device can implement a custom "do I have data/should I process the queue method"
    // For most devices/queues this isn't needed because we want to feed data into/read data from
    // the queue as long as the tail of the queue is lagging behind the head
    // But in some cases like  network devices, we want to wait till slirp has a packet ready for us
    // so this is useful in cases like that
    virtual bool queue_has_data(int queue_idx) = 0;

    inline void ack_interrupt(){
        /*
        What we're supposed to do is this:
        If driver has acknlowedged VIRTIO_MMIO_INT_VRING interrupt by setting bit 0 in interrupt_ack register
        We unset the interrupt by writing 0 to the correct bit in interrupt_register

        What we actually do is: nothing.
        Plic driver in X280 Acks interrupt
        We also skip unsetting the bit in interrupt_status as this isn't needed
        (even though the spec says so, the kernel implementation doesn't rely on it being unset)
        and also happens too fast on the host end.
        Because it happens too fast, the virtio_mmio device's interrupt handler
        (https://elixir.bootlin.com/linux/v6.14.6/source/drivers/virtio/virtio_mmio.c#L317)
        thinks the interrupt was for not of type VIRTIO_MMIO_INT_VRING and doesn't check the virtqueue
        for processed descriptors
        */
        // uint32_t interrupt_status_val = *interrupt_status;
        // uint32_t interrupt_ack_val = *interrupt_ack;
        // if ((interrupt_ack_val & 1)==1) {
        //     // *interrupt_status = ~VIRTIO_MMIO_INT_VRING & interrupt_status_val;
        //     // *interrupt_ack = ~1 & interrupt_ack_val;
        //     // std::lock_guard<std::mutex> guard(interrupt_register_lock);
        //     // *interrupt_register = *interrupt_register & ~(1 << (interrupt_number - 5));
        // }
    }

    inline void set_interrupt(){
        /*
        Set required bit in interrupt_register to 1 if we need to trigger an interrupt
        */
        // uint32_t interrupt_status_val = *interrupt_status;
        if (true){
            //*interrupt_status = VIRTIO_MMIO_INT_VRING | interrupt_status_val;
            std::lock_guard<std::mutex> guard(interrupt_register_lock);
            /*
            FIXME: setting multiple interrupts on the plic seems to be buggy
            so we just set our interrupt instead
            */
            // *interrupt_register = *interrupt_register | (1 << (interrupt_number - 5));
            *interrupt_register = (1 << (interrupt_number - 5));
            __sync_synchronize();
            *interrupt_register = 0;
        }
    }

    void device_setup(){
        /*
        TODO: Draw a state transition diagram here maybe?
        */
        uint32_t prev_doorbell_reg_generation = 0, curr_doorbell_reg_generation=0;
        while (!exit_thread_flag) {
            curr_doorbell_reg_generation = regs->doorbell_reg_generation;
            if (curr_doorbell_reg_generation != prev_doorbell_reg_generation){
                // Doorbell reg is unspported
                regs->doorbell_reg_supported = 1;
                regs->doorbell_reg_address = 0x000430000000;
                regs->doorbell_reg_write_value = 0;
                regs->doorbell_reg_generation = curr_doorbell_reg_generation + 1;
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

        uint8_t inbuf[64];
        uint8_t outbuf[64];
        struct virtio_msg *outhdr = (struct virtio_msg*)outbuf;
        struct virtio_msg *msg = (struct virtio_msg*)inbuf;
        while (!exit_thread_flag){
          memset(inbuf, 0, 64);
          if (spsc_recv(&drv2dev, inbuf, 64)){
            std::cout<<(uint32_t)msg->type<<" "<<(uint32_t)msg->msg_id<<" "<<msg->dev_id<<" "<<msg->msg_size<<"\n";
            if ((msg->type&1) == VIRTIO_MSG_TYPE_REQUEST){
              memset(outbuf, 0, 64);
              switch (msg->msg_id){
                case VIRTIO_MSG_DEVICE_INFO: {
                  struct get_device_info_resp *resp = (struct get_device_info_resp*)(outbuf+sizeof(struct virtio_msg));
                  resp->device_id=device_id;
                  resp->num_feature_bits=64;
                  resp->config_size = device_config_size; 
                  outhdr->type = VIRTIO_MSG_TYPE_RESPONSE;
                  outhdr->msg_id = VIRTIO_MSG_DEVICE_INFO;
                  outhdr->msg_size = 30;
                  spsc_send(&dev2drv, outbuf, 64);
                  set_interrupt();
                  break;
                }
                case VIRTIO_MSG_SET_DEVICE_STATUS: {
                  uint32_t *status = (uint32_t*)(outbuf+sizeof(struct virtio_msg));
                  device_status = *(uint32_t*)(inbuf+sizeof(struct virtio_msg));
                  *status = device_status;
                  outhdr->type = VIRTIO_MSG_TYPE_RESPONSE;
                  outhdr->msg_id = VIRTIO_MSG_SET_DEVICE_STATUS;
                  outhdr->msg_size = 10;
                  spsc_send(&dev2drv, outbuf, 64);
                  set_interrupt();
                  break;
                }
                case VIRTIO_MSG_GET_DEVICE_STATUS: {
                  uint32_t *status = (uint32_t*)(outbuf+sizeof(struct virtio_msg));
                  *status = device_status;
                  outhdr->type = VIRTIO_MSG_TYPE_RESPONSE;
                  outhdr->msg_id = VIRTIO_MSG_GET_DEVICE_STATUS;
                  outhdr->msg_size = 10;
                  spsc_send(&dev2drv, outbuf, 64);
                  set_interrupt();
                  break;
                }
                case VIRTIO_MSG_GET_DEV_FEATURES: {
                  struct get_features *req = (struct get_features*)(inbuf + sizeof(struct virtio_msg));
                  struct get_features_resp *resp = (struct get_features_resp*)(outbuf + sizeof(struct virtio_msg));
                  resp->index = req->index;
                  resp->num = req->num;
                  for (uint32_t features_idx=req->index; features_idx < req->num; features_idx++){
                    *((uint32_t*)(resp->features) + features_idx) = device_features_list[features_idx];
                  }
                  outhdr->type = VIRTIO_MSG_TYPE_RESPONSE;
                  outhdr->msg_id = VIRTIO_MSG_GET_DEV_FEATURES;
                  outhdr->msg_size = sizeof(struct virtio_msg) + sizeof(struct get_features_resp) + sizeof(uint32_t) * resp->num;
                  spsc_send(&dev2drv, outbuf, 64);
                  set_interrupt();
                  break;
                }
                case VIRTIO_MSG_SET_DRV_FEATURES: {
                  struct set_features *req = (struct set_features*)(inbuf + sizeof(struct virtio_msg));
                  for (uint32_t features_idx=req->index; features_idx < req->num; features_idx++){
                    driver_features_list[features_idx] = *((uint32_t*)(req->features) + features_idx);
                  }
                  outhdr->type = VIRTIO_MSG_TYPE_RESPONSE;
                  outhdr->msg_id = VIRTIO_MSG_SET_DRV_FEATURES;
                  outhdr->msg_size = sizeof(struct virtio_msg);
                  spsc_send(&dev2drv, outbuf, 64);
                  set_interrupt();
                  break;
                }
                case VIRTIO_MSG_GET_VQUEUE: {
                  struct get_vqueue *req = (struct get_vqueue*)(inbuf + sizeof(struct virtio_msg));
                  struct get_vqueue_resp *resp = (struct get_vqueue_resp*)(outbuf + sizeof(struct virtio_msg));
                  resp->index = req->index;
                  resp->max_size = 16384;
                  outhdr->type = VIRTIO_MSG_TYPE_RESPONSE;
                  outhdr->msg_id = VIRTIO_MSG_GET_VQUEUE;
                  outhdr->msg_size = sizeof(struct virtio_msg)+sizeof(struct get_vqueue_resp);
                  spsc_send(&dev2drv, outbuf, 64);
                  set_interrupt();
                  break;
                }
                case VIRTIO_MSG_SET_VQUEUE: {
                  struct set_vqueue *req = (struct set_vqueue*)(inbuf + sizeof(struct virtio_msg));
                  descriptor_table_address[req->index]=req->descriptor_addr;
                  used_ring_address[req->index]=req->device_addr;
                  available_ring_address[req->index] = req->driver_addr;
                  outhdr->type = VIRTIO_MSG_TYPE_RESPONSE;
                  outhdr->msg_id = VIRTIO_MSG_SET_VQUEUE;
                  outhdr->msg_size = sizeof(struct virtio_msg);
                  spsc_send(&dev2drv, outbuf, 64);
                  set_interrupt();
                  break;
              }
                case VIRTIO_MSG_GET_CONFIG: {
                  struct get_config *req = (struct get_config*)(inbuf + sizeof(struct virtio_msg));
                  struct get_config_resp *resp = (struct get_config_resp*)(outbuf + sizeof(struct virtio_msg));
                  resp->generation = 0;
                  resp->offset = req->offset;
                  resp->size = req->size;
                  printf("get_config %d %d %d %d\n", device_config[0], device_config[1], device_config[2], device_config[3]);
                  printf("message %d %d\n", req->offset, req->size);
                  memcpy(resp->config, device_config + req->offset, req->size);
                  outhdr->type = VIRTIO_MSG_TYPE_RESPONSE;
                  outhdr->msg_id = VIRTIO_MSG_GET_CONFIG;
                  outhdr->msg_size = sizeof(struct virtio_msg)+sizeof(struct get_config_resp)+req->size;
                  spsc_send(&dev2drv, outbuf, 64);
                  set_interrupt();
                  break;
                }
            }
          }
          }
          usleep(1);
          if (device_status & VIRTIO_CONFIG_S_DRIVER_OK){
              break;
          }
        } 

        for (uint32_t i = 0; i < num_queues; i++) {
            desc[i] = (struct vring_desc*) (memory + (descriptor_table_address[i] - starting_address));
            avail[i] = (struct vring_avail*) (memory + (available_ring_address[i] - starting_address));
            used[i] = (struct vring_used*) (memory + (used_ring_address[i] - starting_address));
        }
    }

    void device_loop(){
        std::vector<uint16_t> processed(num_queues, 0);

        uint8_t inbuf[64];
        uint8_t outbuf[64];
        struct virtio_msg *outhdr = (struct virtio_msg*)outbuf;
        struct virtio_msg *msg = (struct virtio_msg*)inbuf;
        bool check_for_buffers=false;
        struct pollfd pfd = {.fd = chardev_fd, .events = POLLIN};
        while (!exit_thread_flag){
          memset(inbuf, 0, 64);
          while (poll(&pfd, 1, 1) > 0){
            spsc_recv(&drv2dev, inbuf, 64);
            // std::cout<<(uint32_t)msg->type<<" "<<(uint32_t)msg->msg_id<<" "<<msg->dev_id<<" "<<msg->msg_size<<"\n";
            if ((msg->type&1) == VIRTIO_MSG_TYPE_REQUEST){
              memset(outbuf, 0, 64);
              switch (msg->msg_id){
                case VIRTIO_MSG_GET_DEVICE_STATUS: {
                  uint32_t *status = (uint32_t*)(outbuf+sizeof(struct virtio_msg));
                  *status = device_status;
                  outhdr->type = VIRTIO_MSG_TYPE_RESPONSE;
                  outhdr->msg_id = VIRTIO_MSG_GET_DEVICE_STATUS;
                  outhdr->msg_size = 10;
                  spsc_send(&dev2drv, outbuf, 64);
                  set_interrupt();
                  break;
                }
                case VIRTIO_MSG_EVENT_AVAIL: {
                  check_for_buffers = true;
                  break;
                }
              }
            }
          }
          if (!check_for_buffers){
              // continue;
          }
          
            // Process each virtqueue
            for(uint32_t queue_idx=0; queue_idx<num_queues; queue_idx++){
                struct vring_desc *desc_q = desc[queue_idx];
                struct vring_avail *avail_q = avail[queue_idx];
                struct vring_used *used_q = used[queue_idx];
                
                // if (queue_notify_val == i) {
                    __sync_synchronize();
                    bool should_i_set_interrupt=false;
                    uint16_t avail_idx = avail_q->idx;
                    /*
                    processed[i] represents the tail of the queue (our point of view)
                    avail_idx represents the head of the queue (driver's point of view)
                    */
                    if (processed[queue_idx] != avail_idx && queue_has_data(queue_idx)) {
                        should_i_set_interrupt=true;
                        /*
                        avail_q stores a list of descriptors for us to process
                        We pick a desc_idx to process from the avail queue
                        */
                        uint16_t desc_idx = avail_q->ring[processed[queue_idx] % queue_size];
                        uint16_t desc_idx_first = desc_idx;
                        
                        /*
                        desc_idx points to an index of desc_q
                        We either read or write data to that index in the descriptor queue
                        */
                        uint64_t num_bytes_written = 0;
                        uint64_t l = desc_q[desc_idx % queue_size].len;
                        uint64_t a = desc_q[desc_idx % queue_size].addr;
                        uint8_t *addr = memory + (a - starting_address);;
                        
                        /*
                        Sometimes we just process one entry in the desc_q
                        Sometimes the entries have a next flag set 
                        (desc_q[desc_idx].flags & VRING_DESC_F_NEXT)
                        which means that we need to process multiple entries
                        till we encounter an entry without that flag
                        */
                        while (true) {
                            l = desc_q[desc_idx % queue_size].len;
                            a = desc_q[desc_idx % queue_size].addr;
                            addr = memory + (a - starting_address);
                            
                            if ((desc_q[desc_idx % queue_size].flags & VRING_DESC_F_NEXT)) {
                                if (num_bytes_written < queue_header_size) {
                                    process_queue_start(queue_idx, addr, l);
                                } else {
                                    process_queue_data(queue_idx, addr, l);
                                }
                                num_bytes_written += l;
                                desc_idx = desc_q[desc_idx % queue_size].next;
                            } else {
                                process_queue_complete(queue_idx, addr, l);
                                num_bytes_written += l;
                                break;
                            }
                        }
                        
                        /*
                        We then update the used queue to inform the driver
                        that we've processed desc_idx_first in the descriptor queue
                        */
                        uint16_t used_idx = used_q->idx;
                        used_q->ring[used_idx % queue_size].id = desc_idx_first;
                        used_q->ring[used_idx % queue_size].len = num_bytes_written; // Is this the right value? Should it be device dependent
                        __sync_synchronize();
                        used_q->idx = used_idx + 1;

                        processed[queue_idx] += 1;
                    }
                    if (should_i_set_interrupt){
                        // Set interrupt on plic if we processed atleast 1 descriptor
                        struct virtio_msg *outhdr = (struct virtio_msg*)(outbuf);
                        outhdr->type = VIRTIO_MSG_TYPE_RESPONSE;
                        outhdr->msg_id = VIRTIO_MSG_EVENT_USED;
                        outhdr->msg_size = sizeof(struct virtio_msg)+4;
                        struct event_used *resp = (struct event_used*)(outbuf + sizeof(struct virtio_msg));
                        resp->index = queue_idx;
                        spsc_send(&dev2drv, outbuf, 64);
                        set_interrupt();
                    }
                // }
            }
            usleep(1);
        }
    }
    virtual ~VirtioDevice() = default;

    
};
