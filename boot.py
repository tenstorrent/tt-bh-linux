# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

from argparse import ArgumentParser
import sys
import os
from pyluwen import PciChip
from tt_smi.tt_smi_backend import pci_board_reset
import clock
import time
import libfdt

l2cpu_tile_mapping = {
    0: (8, 3),
    1: (8, 9),
    2: (8, 5),
    3: (8, 7)
}

l2cpu_gddr_enable_bit_mapping = {
    0: 5, # L2CPU0 is attached to tt_gddr6_ss_even_inst[2]
    1: 6, # L2CPU1 is attached to tt_gddr6_ss_odd_inst[3]
    2: 7, # L2CPU2 is attached to tt_gddr6_ss_even_inst[3]
    3: 7  # L2CPU3 is attached to tt_gddr6_ss_even_inst[3]
}

def parse_args():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--boot", action='store_true', help="Boot the core after loading the bin files")
    parser.add_argument("--ttdevice", type=int,default=0, help="tenstorrent card to use")
    parser.add_argument("--l2cpu", type=int, nargs="+", default=[0], help="list of L2CPUs to boot")

    # If using FW_PAYLOAD, set these args for rootfs and opensbi
    parser.add_argument("--boot_device", type=str, required=False, default="vda", help="Options: vda, vdaX or initramfs")

    # Only used for initramfs
    parser.add_argument("--rootfs_bin", type=str, required=False, help="Path to initramfs")
    parser.add_argument("--rootfs_dst", type=str, nargs="+", required=True, help="list of Destination address for rootfs for each l2cpu")
    parser.add_argument("--opensbi_bin", type=str, required=True, help="Path to opensbi bin file")
    parser.add_argument("--opensbi_dst", type=str, nargs="+", required=True, help="list of Destination address for opensbi for each l2cpu")

    # If using FW_JUMP with dtb integrated into opensbi, additionally set these kernel args
    parser.add_argument("--kernel_bin", type=str, required=False, help="Path to kernel bin file")
    parser.add_argument("--kernel_dst", type=str, nargs="+", required=False, help="list of Destination address for kernel for each l2cpu")

    # If using FW_JUMP without dtb integrated into opensbi, set these dtb args
    # Requires some opensbi patching to work properly
    # The user can pass in a different device tree for each l2cpu here. The number of dtbs bassed here is the number of l2cpus we boot
    # By default, if the user passes in only 1 value here, we boot l2cpu0 only
    parser.add_argument("--dtb_bin", type=str, nargs="+", required=False, help="list of path to dtb bin file for each l2cpu")
    parser.add_argument("--dtb_dst", type=str, nargs="+", required=False, help="list of Destination address for dtb for each l2cpu")

    args = parser.parse_args()

    assert len(args.l2cpu) == len(args.rootfs_dst) == len(args.opensbi_dst) == len(args.kernel_dst) == len(args.dtb_bin) == len(args.dtb_dst), "Length of all vars must be same"
    for l2cpu in args.l2cpu:
        assert 0 <= l2cpu < 4, "l2cpu IDs must be in [0, 1, 2, 3]"

    return args


def reset_x280(chip, l2cpu_indices):
    reset_unit_base = 0x80030000

    clock.set_l2cpu_pll(chip, 200)
    l2cpu_reset_val = chip.axi_read32(reset_unit_base + 0x14) # L2CPU_RESET
    for l2cpu_index in l2cpu_indices:
        l2cpu_reset_val |= 1 << (l2cpu_index + 4)
    chip.axi_write32(reset_unit_base + 0x14, l2cpu_reset_val) # L2CPU_RESET
    chip.axi_read32(reset_unit_base + 0x14) # L2CPU_RESET
    clock.set_l2cpu_pll(chip, 1750)

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

def main():
    args = parse_args()
    l2cpus_to_boot = args.l2cpu
    pci_board_reset([args.ttdevice])
    f = os.open(f"/dev/tenstorrent/{args.ttdevice}", os.O_RDWR)
    chip = PciChip(args.ttdevice)

    time.sleep(5) # Sleep 5s, telemetry sometimes not available immediately after reset
    telemetry = chip.get_telemetry()
    enabled_l2cpu = telemetry.enabled_l2cpu
    enabled_gddr = telemetry.enabled_gddr
    for l2cpu in l2cpus_to_boot:
        assert (enabled_l2cpu >> l2cpu) & 1, "L2CPU {} is harvested, try booting L2CPU {} with make L2CPU={} boot".format(l2cpu, l2cpu ^ 0b1, l2cpu ^ 0b1)
        assert (enabled_gddr >> l2cpu_gddr_enable_bit_mapping[l2cpu]) & 1, "DRAM attached to L2CPU {} is harvested, try booting L2CPU {} with make L2CPU={} boot".format(l2cpu, l2cpu ^ 0b1, l2cpu ^0b1)
    for idx, l2cpu in enumerate(l2cpus_to_boot):
        (l2cpu_noc_x, l2cpu_noc_y) = l2cpu_tile_mapping[l2cpu]
        l2cpu_base = 0xfffff7fefff10000

        opensbi_addr = int(args.opensbi_dst[idx], 16)
        opensbi_bytes = read_bin_file(args.opensbi_bin)

        if args.rootfs_dst and args.rootfs_bin:
            rootfs_addr = int(args.rootfs_dst[idx], 16)
            rootfs_bytes = read_bin_file(args.rootfs_bin)
            
        if args.kernel_dst and args.kernel_bin:
            kernel_addr = int(args.kernel_dst[idx], 16)
            kernel_bytes = read_bin_file(args.kernel_bin)
        
        if args.dtb_bin and args.dtb_dst:
            dtb_addr = int(args.dtb_dst[idx], 16)
            dtb_bytes = read_bin_file(args.dtb_bin[idx])

        
        fdt = libfdt.Fdt(dtb_bytes)
        # Add some free space to the dtb
        fdt.resize(len(dtb_bytes) + 1000)
        chosen_offset = fdt.path_offset("/chosen", libfdt.QUIET_NOTFOUND)
        if chosen_offset < 0:
            # Device tree doesn't have a chosen node, add chosen node and bootargs value
            chosen_offset = fdt.add_subnode(0, "chosen")

        bootargs = "rw console=hvc0 earlycon=sbi"
        if args.boot_device[:len("vda")] == "vda":
            bootargs += f" root=/dev/{args.boot_device}"
        elif args.boot_device == "initramfs":
            bootargs += f" initrd={args.rootfs_dst[idx]},{len(rootfs_bytes)}"
        else:
            print("Unsupported rootfs type")
            exit(1)
        fdt.setprop(chosen_offset, "bootargs", bytes(bootargs, encoding="utf-8") + b'\0')

        fdt.pack()
        dtb_bytes = fdt._fdt


        # Enable the whole cache when using DRAM
        L3_REG_BASE=0x02010000
        chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, L3_REG_BASE + 8, 0xf)
        data = chip.noc_read32(0, l2cpu_noc_x, l2cpu_noc_y, L3_REG_BASE + 8)

        print(f"Writing OpenSBI to 0x{opensbi_addr:x}")
        chip.noc_write(0, l2cpu_noc_x, l2cpu_noc_y, opensbi_addr, opensbi_bytes)

        if args.rootfs_dst and args.rootfs_bin:
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
        reset_x280(chip, l2cpus_to_boot)
    else:
        print("Not booting (you didn't pass --boot)")

    for l2cpu in l2cpus_to_boot:
        (l2cpu_noc_x, l2cpu_noc_y) = l2cpu_tile_mapping[l2cpu]
        # Configure L2 prefetchers
        L2_PREFETCH_BASE=0x02030000
        for offset in (0x0000, 0x2000, 0x4000, 0x6000):
            chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, L2_PREFETCH_BASE+offset, 0x15811)
            chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, L2_PREFETCH_BASE+4+offset, 0x38c84e)


if __name__ == "__main__":
    main()
    sys.exit(0)
