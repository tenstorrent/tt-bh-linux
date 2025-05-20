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
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
extern "C" {
#define class __class_compat // Rename 'class' to avoid C++ keyword conflict

#include <linux/virtio_blk.h>
#include <linux/virtio_ring.h>

#ifdef class
#undef class // Undefine our temporary macro if it was defined
#endif
}

#include "l2cpu.h"

#define DISK_SHM_REGION_OFFSET ((2ULL * 1024 * 1024))

#define X280_REGISTERS 0xFFFFF7FEFFF10000ULL
#define DISK_INTERRUPT_NUMBER 33

#define PACKET_SIZE 1514
namespace disk{
struct base_config {
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

struct shared_data {
  struct base_config base;
  struct virtio_blk_config device;
};

uint32_t device_features_list[2];
uint32_t driver_features_list[2];

#define QUEUE_SIZE 1024

int disk_loop(int l2cpu_idx, std::atomic<bool> &exit_thread_flag) {

  L2CPU l2cpu(l2cpu_idx);

  // Assumes that 2M memory region is at the region just before the beginning of
  // th pmem, which itself is at the end of the dram
  uint64_t address = l2cpu.get_starting_address() + l2cpu.get_memory_size() - DISK_SHM_REGION_OFFSET;

  uint64_t starting_address = l2cpu.get_starting_address();

  auto window = l2cpu.get_persistent_2M_tlb_window(address);
  struct shared_data *base =
      reinterpret_cast<struct shared_data *>(window->get_window());

  uint64_t interrupt_address =
      X280_REGISTERS + 0x404; // X280 Global Interrupts Register Bits 31 to 0
  // For some reason, interrupts 6->9 don't work for this even though I think
  // they should Only interrupts 0->5 are reserved, don't know if 6->9 are also
  // connected to something else Interrupts are triggered by setting bit
  // INTERRUPT_NUMBER-5 in the register referenced above
  assert(DISK_INTERRUPT_NUMBER >= 10 && DISK_INTERRUPT_NUMBER <= 36);
  auto interrupt_address_window =
      l2cpu.get_persistent_2M_tlb_window(interrupt_address);
  uint32_t *interrupt_register =
      reinterpret_cast<uint32_t *>(interrupt_address_window->get_window());

  uint8_t* memory = l2cpu.get_memory_ptr();

  #define SECTOR_SIZE 512
  int fd = open("debian-riscv64.img", O_RDWR);
  if (fd == -1){
    perror("Failed to open file");
    return -1;
  }

  struct stat sb;
  if (fstat(fd, &sb) == -1) {
      perror("Failed to get file size");
      close(fd);
      return -1;
  }
  size_t file_size = sb.st_size;
  size_t num_sectors = (file_size + SECTOR_SIZE - 1) / SECTOR_SIZE; // Ceiling division
  printf("File size %lu", file_size);
  printf("Nun sectors %lu", num_sectors);

  uint8_t *mapped_data = (uint8_t*)mmap(NULL, file_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_PRIVATE, fd, 0);
  if (mapped_data == MAP_FAILED) {
      perror("Failed to mmap file");
      close(fd);
      return -1;
  }


  memset(base, 0, sizeof(struct shared_data));
  base->base.magic_value = 0x74726976;
  base->base.version = 0x2;
  base->base.device_id = 2;

  base->base.device_features = 1;
  device_features_list[0] = 0;
  device_features_list[1] = 1;

  base->base.queue_num_max = QUEUE_SIZE;

  base->device.capacity = num_sectors;

  // int len, ret_code;

  while (!exit_thread_flag) {
    __sync_synchronize();
    if (base->base.status & 2) {
      break;
    }
  }
  while (!exit_thread_flag) {
    base->base.device_features =
        device_features_list[base->base.device_features_select];
    __sync_synchronize();

    // Implement driver_features_select logic too here

    if (base->base.status & 8) {
      break;
    }
  }
#define DISK_NUM_QUEUES 1
  struct vring_desc *desc[DISK_NUM_QUEUES];
  struct vring_avail *avail[DISK_NUM_QUEUES];
  struct vring_used *used[DISK_NUM_QUEUES];
  uint64_t descriptor_table_address[DISK_NUM_QUEUES];
  uint64_t available_ring_address[DISK_NUM_QUEUES];
  uint64_t used_ring_address[DISK_NUM_QUEUES];
  // uint32_t queue_ready_list[2];
  // queue_ready_list[0] = 0;
  // queue_ready_list[1] = 0;
  printf("queue_ready is at %ld\n",
         (uint8_t *)&(base->base.queue_ready) - (uint8_t *)base);
  uint32_t prev_queue_select = -1;
  while (!exit_thread_flag) {
    __sync_synchronize();
    uint32_t queue_select = base->base.queue_select;
    uint32_t queue_ready = base->base.queue_ready;
    base->base.queue_ready = 0;
    __sync_synchronize();

    // printf("queue_select %d queue_ready %d\n", queue_select, queue_ready);
    if (queue_ready && (queue_select != prev_queue_select)) {
      printf("queue_select %d\n", queue_select);
      __sync_synchronize();
      descriptor_table_address[queue_select] =
          ((uint64_t)base->base.queue_desc_high << 32) |
          base->base.queue_desc_low;
      available_ring_address[queue_select] =
          ((uint64_t)base->base.queue_avail_high << 32) |
          base->base.queue_avail_low;
      used_ring_address[queue_select] =
          ((uint64_t)base->base.queue_used_high << 32) |
          base->base.queue_used_low;
      // queue_ready_list[queue_select] = 1;
      prev_queue_select = queue_select;
      printf("%ld %ld %ld\n", descriptor_table_address[queue_select],
             available_ring_address[queue_select],
             used_ring_address[queue_select]);
      //__sync_synchronize();
      // if (queue_ready_list[NUM_QUEUES - 1] == 1)
      if (queue_select == (DISK_NUM_QUEUES - 1))
        break;
    }
    usleep(1);
  }
  for (int i = 0; i < DISK_NUM_QUEUES; i++) {
    printf("%ld %ld %ld\n", descriptor_table_address[i],
           available_ring_address[i], used_ring_address[i]);
    desc[i] = (struct vring_desc*) (memory + (descriptor_table_address[i] - starting_address));
    avail[i] = (struct vring_avail*) (memory + (available_ring_address[i] - starting_address));
    used[i] = (struct vring_used*) (memory + (used_ring_address[i] - starting_address));
  }
  // Initialization phase done?
  uint16_t idx = 0;
  volatile struct vring_desc *desc_q = desc[0];
  volatile struct vring_avail *avail_q = avail[0];
  volatile struct vring_used *used_q = used[0];

  while (!exit_thread_flag) {
    if (base->base.magic_value != 0x74726976) {
      return 0;
    }

    __sync_synchronize();
    uint32_t queue_notify = base->base.queue_notify;
    uint32_t interrupt_status = base->base.interrupt_status;
    uint32_t interrupt_ack = base->base.interrupt_ack;
    if ((interrupt_ack & 1) == 1) {
      base->base.interrupt_status = ~1 & interrupt_status;
      base->base.interrupt_ack = ~1 & interrupt_ack;
      *interrupt_register =
          (*interrupt_register) & ~(1 << (DISK_INTERRUPT_NUMBER - 5));
      // printf("Unsetting interrupt\n");
    }
    if (queue_notify == 0) {
      // Get bytes transmitted by driver
      __sync_synchronize();
      uint16_t avail_idx = avail_q->idx;
      if (idx != avail_idx) {
        // printf("idx: %u avail->tx_idx %u flags %d\n", idx, avail_idx,
        //        avail_q->flags);
        uint16_t desc_idx = avail_q->ring[idx % QUEUE_SIZE];
        uint16_t desc_idx_head = desc_idx;
        uint16_t used_idx = used_q->idx;

        uint64_t num_bytes_written = 0;
        uint64_t l = desc_q[desc_idx % QUEUE_SIZE].len;
        uint64_t a = desc_q[desc_idx % QUEUE_SIZE].addr;
        uint8_t *addr = memory + (a - starting_address);;

        struct virtio_blk_outhdr *req = (struct virtio_blk_outhdr *)addr;
        if (req->type !=0 && req->type !=1)
          printf("Type: %d Sector: %llu\n", req->type, req->sector);
        while (true) {
          if (req->type !=0 && req->type !=1)
            printf("Flags: %d Len: %u\n", desc_q[desc_idx].flags, desc_q[desc_idx].len);
          l = desc_q[desc_idx % QUEUE_SIZE].len;
          a = desc_q[desc_idx % QUEUE_SIZE].addr;
          addr = memory + (a - starting_address);

          if (num_bytes_written < sizeof(struct virtio_blk_outhdr)) {
          } else if ((desc_q[desc_idx].flags & 1)) {
            if (req->type==0){
              memcpy(addr, mapped_data + ((SECTOR_SIZE * req->sector) + num_bytes_written-sizeof(struct virtio_blk_outhdr)), l);
            } else if (req->type==1){
              memcpy(mapped_data + ((SECTOR_SIZE * req->sector) + num_bytes_written-sizeof(struct virtio_blk_outhdr)), addr, l);
            }
          } else {
            *addr = 0;
            // num_bytes_written += 1;
            break;
          }
          num_bytes_written += l;
          desc_idx = desc_q[desc_idx % QUEUE_SIZE].next;
        }

        used_q->ring[used_idx % QUEUE_SIZE].id = desc_idx_head;
        used_q->ring[used_idx % QUEUE_SIZE].len =
            num_bytes_written - sizeof(struct virtio_blk_outhdr);
        __sync_synchronize();
        used_q->idx = used_idx + 1;
        __sync_synchronize();
        base->base.interrupt_status = 1 | interrupt_status;
        *interrupt_register = (1 << (DISK_INTERRUPT_NUMBER - 5));
        __sync_synchronize();
        // printf("Setting interrupt\n");
        idx += 1;
        // printf("Recv %d\n", idx);
      }
    }
    usleep(1);
  }
  munmap(mapped_data, file_size);
  close(fd);
  return 0;
}
}
// int main(int argc, char **argv){
//     int l2cpu=0;
//     const char* const short_opts = "l:h";
//     const option long_opts[] = {
//             {"l2cpu", required_argument, nullptr, 'l'},
//             {"help", no_argument, nullptr, 'h'},
//             {nullptr, no_argument, nullptr, 0}
//     };
//
//     while (true)
//     {
//         const auto opt = getopt_long(argc, argv, short_opts, long_opts,
//         nullptr);
//
//         if (-1 == opt)
//             break;
//
//         switch (opt)
//         {
//         case 'l':
//             l2cpu = std::stoi(optarg);
//             break;
//
//         case 'h': // -h or --help
//         case '?': // Unrecognized option
//         default:
//             std::cout <<
//             "--l2cpu <l>:         L2CPU to attach to\n"
//             "--help:              Show help\n";
//             exit(1);
//         }
//     }
//
//     if (l2cpu < 0 || l2cpu > 3){
//         std::cerr<<"l2cpu must be one of 0,1,2,3"<<"\n";
//         exit(1);
//     }
//
//     while (true){
//         /*
//         We call run_once in a loop
//         If the chip resets or the MAGIC value disappears,
//         we sleep for a bit and try setting everything up again
//         */
//         run_once(l2cpu);
//         std::cout<<"Sleeping for a bit and trying again"<<"\n";
//         usleep(100000);
//     }
// }
