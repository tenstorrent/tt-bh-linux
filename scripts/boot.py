#!/usr/bin/env python

from argparse import ArgumentParser
import sys
import os
from pyluwen import PciChip
import clock

def parse_args():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--l2cpu", type=int, default=0, help="Which L2CPU")
    parser.add_argument("--boot", action='store_true', help="Boot the core after loading the bin files")

    # If using FW_PAYLOAD, set these args for rootfs and opensbi
    parser.add_argument("--rootfs_bin", type=str, required=True, help="Path to rootfs bin file")
    parser.add_argument("--rootfs_dst", type=str, required=True, help="Destination address for rootfs")
    parser.add_argument("--opensbi_bin", type=str, required=True, help="Path to opensbi bin file")
    parser.add_argument("--opensbi_dst", type=str, required=True, help="Destination address for opensbi")
    
    # If using FW_JUMP with dtb integrated into opensbi, additionally set these kernel args
    parser.add_argument("--kernel_bin", type=str, required=False, help="Path to kernel bin file")
    parser.add_argument("--kernel_dst", type=str, required=False, help="Destination address for kernel")
    
    # If using FW_JUMP without dtb integrated into opensbi, set these dtb args
    # Requires some opensbi patching to work properly
    parser.add_argument("--dtb_bin", type=str, required=False, help="Path to dtb bin file")
    parser.add_argument("--dtb_dst", type=str, required=False, help="Destination address for dtb")

    args = parser.parse_args()
    return args


def reset_x280(chip, l2cpu_index):
    reset_unit_base = 0x80030000
    clock.main(0, 200)
    l2cpu_reset_val = chip.axi_read32(reset_unit_base + 0x14) # L2CPU_RESET
    l2cpu_reset_val |= 1 << (l2cpu_index + 4)
    chip.axi_write32(reset_unit_base + 0x14, l2cpu_reset_val) # L2CPU_RESET
    chip.axi_read32(reset_unit_base + 0x14) # L2CPU_RESET
    clock.main(0, 1750)

def read_bin_file(file_path):
    with open(file_path, 'rb') as file:
        file_bytes = file.read()
        # Calculate the number of bytes to pad
        padding = len(file_bytes) % 4
        if padding != 0:
            # Calculate the number of bytes needed to pad
            padding_bytes_needed = 4 - padding
            # Pad the bytes with zeros
            file_bytes += b'\x00' * padding_bytes_needed
    return file_bytes

def conf_l2cpu_noc_tlb_2M(chip, l2cpu_index, tlb_entry, x, y, addr):
    strict_order = 1
    addr = addr >> 21
    l2cpu_noc_tlb_base = 0xFFFFF7FEFFF00000
    (l2cpu_noc_x, l2cpu_noc_y) = {
        0: (8, 3),
    }[l2cpu_index]
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_noc_tlb_base + 16 * tlb_entry, (addr & 0xffffffff)) # l2cpu_noc_tlb.NOC_TLB_GROUP_0_ADDR_LOWER_{tlb_entry}
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_noc_tlb_base + 16 * tlb_entry + 4, (addr >> 32) & 0x7ff) # l2cpu_noc_tlb.NOC_TLB_GROUP_0_ADDR_UPPER_{tlb_entry}
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_noc_tlb_base + 16 * tlb_entry + 8, strict_order << 25 | y << 6| x << 0) # l2cpu_noc_tlb.NOC_TLB_GROUP_0_MISC_0_{tlb_entry}
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_noc_tlb_base + 16 * tlb_entry + 12, 0x0) # l2cpu_noc_tlb.NOC_TLB_GROUP_0_MISC_1_{tlb_entry}
    return 0x002030000000 + 0x200000 * tlb_entry    # Address of the window in X280 address space

def conf_l2cpu_noc_tlb_128G(chip, l2cpu_index, tlb_entry, x, y, addr):
    strict_order = 1
    addr = addr >> 37
    l2cpu_noc_tlb_base = 0xFFFFF7FEFFF00000
    (l2cpu_noc_x, l2cpu_noc_y) = {
        0: (8, 3),
    }[l2cpu_index]
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_noc_tlb_base + 0xE00 + 12 * tlb_entry, (addr & 0x7ffffff)) # l2cpu_noc_tlb.NOC_TLB_GROUP_1_{tlb_entry}
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_noc_tlb_base + 0xE00 + 12 * tlb_entry + 4, strict_order << 25 | y << 6 | x) # l2cpu_noc_tlb.NOC_TLB_GROUP_1_MISC_0_{tlb_entry}
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_noc_tlb_base + 0xE00 + 12 * tlb_entry + 8, 0x0) # l2cpu_noc_tlb.NOC_TLB_GROUP_1_MISC_1_{tlb_entry}
    # return (1 << 43) | (1 << 37) * (1 + tlb_entry) | 0x30000000
    return 0x400030000000 | (1 << 43) | ((1 << 37) * (1 + tlb_entry))

def conf_pcie_rc_noc_tlb_data(chip):
    (pcie_rc_noc_x, pcie_rc_noc_y) = (11, 0)
    SII_A = 0xFFFFFFFFF0000000;

    # 17 picked as arbitrary index
    config_address = 0x134 + 0x4 * 17
    access_address = 17 << 58
    chip.noc_write32(0, pcie_rc_noc_x, pcie_rc_noc_y, SII_A + config_address, 0b100); # CFG0

    chip.noc_write32(0, pcie_rc_noc_x, pcie_rc_noc_y, access_address + 0x0, 0x0)
    return access_address

def uart_init(chip, l2cpu_index):
    reset_unit_base = 0x80030000
    (uart_block_noc_x, uart_block_noc_y) = (8, 0)
    DLL_ADDR = 0x80200000 #RBR interperted as Divisor Latch Low  when LCR[7] =1
    DLH_ADDR = 0x80200004 #IEH interperted as Divisor Latch High when LCR[7] =1
    FCR_ADDR = 0x80200008 # WRITE only
    LCR_ADDR = 0x8020000C
    DLF_ADDR = 0x802000C0
    UART_ADDRESS_BLOCK_LCR_REG_DEFAULT = 0x00000000

    gpio4_pad_trien_cntl = chip.axi_read32(reset_unit_base + 0x5a0) # GPIO4_PAD_TRIEN_CNTL
    gpio4_pad_rxen_cntl  = chip.axi_read32(reset_unit_base + 0x5ac) # GPIO4_PAD_RXEN_CNTL
    chip.axi_write32(reset_unit_base + 0x5a0, (gpio4_pad_trien_cntl | 2) & 0xfffffe) # GPIO4_PAD_TRIEN_CNTL
    chip.axi_write32(reset_unit_base + 0x5ac , (gpio4_pad_rxen_cntl  | 2) & 0xfffffe) # GPIO4_PAD_RXEN_CNTL

    uart_cntl = chip.axi_read32(reset_unit_base + 0x608) # UART_CNTL
    uart_enable = 0x3
    chip.axi_write32(reset_unit_base + 0x608, uart_cntl | uart_enable) # UART_CNTL

    lcr_data_dlab = 0x1
    chip.noc_write32(0, uart_block_noc_x, uart_block_noc_y, LCR_ADDR,UART_ADDRESS_BLOCK_LCR_REG_DEFAULT | (lcr_data_dlab << 7))
    chip.noc_write32(0, uart_block_noc_x, uart_block_noc_y, DLL_ADDR,0x45)
    chip.noc_write32(0, uart_block_noc_x, uart_block_noc_y, DLH_ADDR,0x1)
    chip.noc_write32(0, uart_block_noc_x, uart_block_noc_y, DLF_ADDR,0x3)
    chip.noc_write32(0, uart_block_noc_x, uart_block_noc_y, FCR_ADDR,0x1)

    lcr_data_dlab = 0x0
    lcr_data_dls  = 0x3
    chip.noc_write32(0, 8, 0, LCR_ADDR,UART_ADDRESS_BLOCK_LCR_REG_DEFAULT | (lcr_data_dlab << 7) | lcr_data_dls)
    conf_l2cpu_noc_tlb_2M(chip, l2cpu_index, 0, 8, 0, 0x080200000)

def main():
    args = parse_args()
    chip = PciChip(0)
    (l2cpu_noc_x, l2cpu_noc_y) = {
        0: (8, 3),
    }[args.l2cpu]
    l2cpu_base = 0xfffff7fefff10000

    # No UART in scrappy
    # if args.uart:
    #     uart_init(chip, args.l2cpu)

    # No pcie RC config needed in scrappy
    # # Address within PCIe RC tile where config space is visible.
    # # Need to map a 2MiB TLB window at it.
    # pcie_config_space_rc_tile = conf_pcie_rc_noc_tlb_data(chip)
    # print(f"PCIe config space address in RC tile: 0x{pcie_config_space_rc_tile:x}")
    # pcie_config_space_x280 = conf_l2cpu_noc_tlb_2M(chip, args.l2cpu, 17, 11, 0, pcie_config_space_rc_tile)
    # print(f"PCIe config space address in L2CPU tile: 0x{pcie_config_space_x280:x}")
    # pcie_space_x280 = conf_l2cpu_noc_tlb_2M(chip, args.l2cpu, 18, 11, 0, 0)
    # print(f"PCIe space address in L2CPU tile: 0x{pcie_space_x280:x}")

    # This config can be used if you want to map another dram tile
    # to either add more memory or put rootfs in one of the tiles
    # somewhere = conf_l2cpu_noc_tlb_128G(chip, args.l2cpu, 0, 0, 0, 0x0)
    # print(f"TLB128 entry 0: 0x{somewhere:x}")

    opensbi_addr = int(args.opensbi_dst, 16)
    rootfs_addr = int(args.rootfs_dst, 16)
    opensbi_bytes = read_bin_file(args.opensbi_bin)
    rootfs_bytes = read_bin_file(args.rootfs_bin)
    if args.kernel_dst and args.kernel_bin:
        kernel_addr = int(args.kernel_dst, 16)
        kernel_bytes = read_bin_file(args.kernel_bin)
    
    if args.dtb_bin and args.dtb_dst:
        dtb_addr = int(args.dtb_dst, 16)
        dtb_bytes = read_bin_file(args.dtb_bin)


    # Enable the whole cache when using DRAM
    L3_REG_BASE=0x02010000
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, L3_REG_BASE + 8, 0xf)
    data = chip.noc_read32(0, l2cpu_noc_x, l2cpu_noc_y, L3_REG_BASE + 8)

    print(f"Writing OpenSBI to 0x{opensbi_addr:x}")
    chip.noc_write(0, l2cpu_noc_x, l2cpu_noc_y, opensbi_addr, opensbi_bytes)
    print(f"Writing rootfs to 0x{rootfs_addr:x}")
    chip.noc_write(0, l2cpu_noc_x, l2cpu_noc_y, rootfs_addr, rootfs_bytes)

    if args.kernel_dst and args.kernel_bin:
        print(f"Writing Kernel to 0x{kernel_addr:x}")
        chip.noc_write(0, l2cpu_noc_x, l2cpu_noc_y, kernel_addr, kernel_bytes)
    if args.dtb_dst and args.dtb_bin:
        print(f"Writing dtb to 0x{dtb_addr:x}")
        chip.noc_write(0, l2cpu_noc_x, l2cpu_noc_y, dtb_addr, dtb_bytes)

    reset_vector_0 = opensbi_addr & 0xffffffff
    reset_vector_1 = opensbi_addr >> 32

    
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_base + 0x0, reset_vector_0) # l2cpu.RESET_VECTOR_CORE_0_0
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_base + 0x4, reset_vector_1) # l2cpu.RESET_VECTOR_CORE_0_1
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_base + 0x8, reset_vector_0) # l2cpu.RESET_VECTOR_CORE_1_0
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_base + 0xC, reset_vector_1) # l2cpu.RESET_VECTOR_CORE_1_1
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_base + 0x10, reset_vector_0) # l2cpu.RESET_VECTOR_CORE_2_0
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_base + 0x14, reset_vector_1) # l2cpu.RESET_VECTOR_CORE_2_1
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_base + 0x18, reset_vector_0) # l2cpu.RESET_VECTOR_CORE_3_0
    chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, l2cpu_base + 0x1C, reset_vector_1) # l2cpu.RESET_VECTOR_CORE_3_1
    # input("Waiting")

    if args.boot:
        reset_x280(chip, args.l2cpu)
    else:
        print("Not booting (you didn't pass --boot)")

    # Configure L2 prefetchers
    L2_PREFETCH_BASE=0x02030000
    for offset in (0x0000, 0x2000, 0x4000, 0x6000):
        chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, L2_PREFETCH_BASE+offset, 0x15811)
        chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, L2_PREFETCH_BASE+4+offset, 0x38c84e)


if __name__ == "__main__":
    main()
    sys.exit(0)
