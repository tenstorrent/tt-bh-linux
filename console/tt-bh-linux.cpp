// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <mutex> // Added for std::mutex
#include <string> // Added for std::string
#include <iostream> // Added for std::cout, std::cerr
#include <getopt.h> // Added for getopt_long
#include <thread> // Added for std::thread
#include <fcntl.h>

#include "console.hpp"
#include "disk.hpp"
#include "network.hpp"

std::atomic<bool> exit_thread_flag{false};
std::mutex interrupt_register_lock; // Global mutex for MMIO access

void console_main(int l2cpu){
    printf("Press Ctrl-A x to exit.\n\n");
    while (!exit_thread_flag) {
        try {
            int r = uart_loop(l2cpu, exit_thread_flag);
            if (r == -EAGAIN) {
                printf("Error (UART vanished) -- was the chip reset?  Retrying...\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                exit_thread_flag = true;
                return;
            }
        } catch (const std::exception& e) {
            printf("Error (%s) -- was the chip reset?  Retrying...\n", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void disk_main(int l2cpu, std::mutex& interrupt_register_lock, int interrupt_number, uint64_t mmio_region_offset, const std::string& disk_image_path, bool virtio_msg_msi_supported, uint64_t msi_addr, uint32_t msi_value){
    while (!exit_thread_flag){
        VirtioBlk device(l2cpu, exit_thread_flag, interrupt_register_lock, interrupt_number, mmio_region_offset, disk_image_path, virtio_msg_msi_supported, msi_addr, msi_value);
        device.device_setup();
        device.device_loop();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void network_main(int l2cpu, std::mutex& interrupt_register_lock, int interrupt_number, uint64_t mmio_region_offset, bool virtio_msg_msi_supported, uint64_t msi_addr, uint32_t msi_value){
    while (!exit_thread_flag){
        VirtioNet device(l2cpu, exit_thread_flag, interrupt_register_lock, interrupt_number, mmio_region_offset, virtio_msg_msi_supported, msi_addr, msi_value);
        device.device_setup();
        device.device_loop();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

uint64_t conf_l2cpu_noc_tlb_2M(int l2cpu_idx, uint32_t tlb_entry, uint32_t x, uint32_t y, uint64_t addr){
    L2CPU l2cpu(l2cpu_idx);

    uint32_t strict_order = 1;
    addr = addr >> 21;
    uint64_t l2cpu_noc_tlb_base = 0x0000'2000'0000ULL;
    l2cpu.write32(l2cpu_noc_tlb_base + 16 * tlb_entry, (addr & 0xffffffff)); // l2cpu_noc_tlb.NOC_TLB_GROUP_0_ADDR_LOWER_{tlb_entry}
    l2cpu.write32(l2cpu_noc_tlb_base + 16 * tlb_entry + 4, (addr >> 32) & 0x7ff); // l2cpu_noc_tlb.NOC_TLB_GROUP_0_ADDR_UPPER_{tlb_entry}
    l2cpu.write32(l2cpu_noc_tlb_base + 16 * tlb_entry + 8, strict_order << 25 | y << 6| x << 0); // l2cpu_noc_tlb.NOC_TLB_GROUP_0_MISC_0_{tlb_entry}
    l2cpu.write32(l2cpu_noc_tlb_base + 16 * tlb_entry + 12, 0x0); // l2cpu_noc_tlb.NOC_TLB_GROUP_0_MISC_1_{tlb_entry}
    return 0x0004'3000'0000ULL + 0x200000ULL * tlb_entry;    // Address of the window in X280 address space

}

int main(int argc, char **argv){
    int l2cpu=0;
    std::string disk_image_path = "rootfs.ext4";
    std::string cloud_init_path = "";
    bool virtio_msg_msi = false;

    const char* const short_opts = "l:d:c:h";
    const option long_opts[] = {
            {"l2cpu", required_argument, nullptr, 'l'},
            {"disk", required_argument, nullptr, 'd'},
            {"cloud-init", required_argument, nullptr, 'c'},
            {"virtio-msg-msi", no_argument, nullptr, 'm'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, no_argument, nullptr, 0}
    };

    while (true)
    {
        const auto opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);

        if (-1 == opt)
            break;

        switch (opt)
        {
        case 'l':
            l2cpu = std::stoi(optarg);
            break;
        case 'd': // Handle disk image option
            disk_image_path = optarg;
            break;
        case 'c': // Handle cloud init option
            cloud_init_path = optarg;
            break;
        case 'm': // Handle cloud init option
            virtio_msg_msi = true;
            break;
        case 'h': // -h or --help
        case '?': // Unrecognized option
        default:
            std::cout <<
            "--l2cpu <l>:         L2CPU to attach to\n"
            "--disk <path>:       Path to the disk image (default: rootfs.ext4)\n"
            "--cloud-init <path>:   Path to the cloud-init image (optional)\n"
            "--virtio-msg-msi:    Try to map MSI Addr on PCIe Tile to x280 and run virtio-msg with 2 way interrupts if successful\n"
            "--help:              Show help\n";
            exit(1);
        }
    }

    if (l2cpu < 0 || l2cpu > 3){
        std::cerr<<"l2cpu must be one of 0,1,2,3"<<"\n";
        exit(1);
    }

  uint64_t msi_addr;
  uint32_t msi_value;
  if (virtio_msg_msi){
    // Try to determine MSI Addr and value from PCIe Tile
    int fd = open("/dev/tenstorrent/0", O_RDWR | O_CLOEXEC);
    uint32_t pcie_tile_x = 19, pcie_tile_y = 24;
    uint64_t pcie_dbi_addr = 0xF800'0000'0000'0000ULL;
    TlbWindow2M window(fd, pcie_tile_x, pcie_tile_y, pcie_dbi_addr);
    msi_addr = (((uint64_t)window.read32(0x58)) << 32) | window.read32(0x54);
    msi_value = window.read32(0x5C);
    close(fd);
    if (msi_addr == ~0ULL){ // If we read all Fs, something is maybe wrong
      std::cerr<<"failed to get MSI Addr. Try running in 1 way polling mode with make boot_poll\n";
      exit(1);
    } else {
      // Map the first (or last?) 2M Uncached x280 NOC Outbound TLB Window to MSI Addr on PCIe Tile
      msi_addr = conf_l2cpu_noc_tlb_2M(l2cpu, 0, pcie_tile_x, pcie_tile_y, msi_addr) + (msi_addr&((1ULL<<21)-1));
    }
  }

  std::vector<std::thread> threads;
  threads.emplace_back(console_main, l2cpu);
  threads.emplace_back(disk_main, l2cpu, std::ref(interrupt_register_lock), 33, 6ULL*4*1024, disk_image_path, virtio_msg_msi, msi_addr, msi_value);
  threads.emplace_back(network_main, l2cpu, std::ref(interrupt_register_lock), 32, 9ULL*4*1024, virtio_msg_msi, msi_addr, msi_value);
  if (!cloud_init_path.empty()) {
    threads.emplace_back(disk_main, l2cpu, std::ref(interrupt_register_lock), 31, 3ULL*4*1024, cloud_init_path, virtio_msg_msi, msi_addr, msi_value);
  }
  for (auto& thread: threads){
    thread.join();
  }
}
