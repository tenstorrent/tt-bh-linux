#include <atomic>
#include <cassert>
#include <cstdio>
#include <getopt.h>
#include <inttypes.h>
#include <iostream>
#include <queue>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <string> // Added for std::string
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <mutex> // Added for std::mutex
extern "C" {
#define class __class_compat // Rename 'class' to avoid C++ keyword conflict

#include <linux/virtio_blk.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_mmio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>

#ifdef class
#undef class // Undefine our temporary macro if it was defined
#endif
}

#include "l2cpu.h"
#include "virtiodevice.hpp"

class VirtioBlk : public VirtioDevice {
public:
    int sector_size = 512;
    int fd = -1;
    uint8_t* mapped_data = nullptr;
    size_t file_size = 0;
    size_t num_sectors = 0;
    struct virtio_blk_outhdr *req;
    std::string disk_image_path;

    VirtioBlk(int l2cpu_idx, std::atomic<bool>& exit_flag, std::mutex& interrupt_register_lock, int interrupt_number_, uint64_t mmio_region_offset_, const std::string& image_path)
        : VirtioDevice(l2cpu_idx, exit_flag, interrupt_register_lock, interrupt_number_, mmio_region_offset_), disk_image_path(image_path) {
        // FIXME: I don't know if we're handling the last sector's size, reads and writes properly
        
        num_queues = 1;
        device_features_list[0] = 0;
        device_features_list[1] = 1<<(VIRTIO_F_VERSION_1-32);
        
        fd = open(disk_image_path.c_str(), O_RDWR);
        if (fd == -1) {
            perror(("Failed to open file: " + disk_image_path).c_str());
            return;
        }
        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            perror("Failed to get file size");
            close(fd);
            fd = -1;
            return;
        }
        file_size = sb.st_size;
        num_sectors = (file_size + sector_size - 1) / sector_size;
        mapped_data = (uint8_t*)mmap(NULL, file_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapped_data == MAP_FAILED) {
            perror("Failed to mmap file");
            close(fd);
            fd = -1;
            return;
        }
        // *device_id = VIRTIO_ID_BLOCK;

        // struct virtio_blk_config *device_config = reinterpret_cast<struct virtio_blk_config*>(mmio_base + VIRTIO_MMIO_CONFIG);
        // device_config->capacity = num_sectors;

        queue_header_size = sizeof(struct virtio_blk_outhdr);
    }

    void process_queue_start(int queue_idx, uint8_t* addr, uint64_t len) override {
        assert(queue_idx==0);
        req = (struct virtio_blk_outhdr*)addr;
    }

    void process_queue_data(int queue_idx, uint8_t* addr, uint64_t len) override {
        /*
        FIXME: x280 side driver seems to use some kind of in memory cache 
        and coalesces writes. write speeds are abnormally high when cache is used and
        then plummet when a bunch of coalesced writes are dumped to this program to
        handle (which it can't handle fast enough)

        A consequence of this is that a userspace program running on X280 thinks that a 
        write may have happened even though the request for the write was not yet processed
        by this program on host

        If you try running this
        dd if=/dev/random of=1.img count=1024 bs=1M status=progress

        you'll see that the number of bytes written "increases" for a bit and then "stops"
        for a while, then resumes increasing again

        the driver sends a bunch of write requests over the queue to this program only
        when the dd command is in it's "stopped" stage.

        This results in weird behaviour like being unable to interrupt the dd command
        (or any other command doing writes) till those writes are processed by this program

        Similarly, wget's from host to x280 may show abnormal speeds while the cache is in use
        */

        // Only one queue, so queue_idx is always 0
        assert(queue_idx==0);

        // Use req->type to determine read/write
        switch (req->type) {
            case VIRTIO_BLK_T_IN:
                memcpy(addr, mapped_data + ((sector_size * req->sector)), len);
                break;
            case VIRTIO_BLK_T_OUT:
                memcpy(mapped_data + ((sector_size * req->sector)), addr, len);
                break;
            default:
                printf("Unimplemented Request Type: %d Len: %lu\n", req->type, len);
        }
    }

    void process_queue_complete(int queue_idx, uint8_t* addr, uint64_t len) override {
        assert(queue_idx==0);
        // For block device, set status byte to 0 (success)
        *addr = 0;
    }

    inline bool queue_has_data(int queue_idx){
        return true;
    }

};
