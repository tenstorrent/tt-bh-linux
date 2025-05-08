#!/bin/bash

# Adjusted for FS of size 1200 MB
     FS_ADDR="0x4000e5000000"
PAYLOAD_ADDR="0x400030000000"
 KERNEL_ADDR="0x400030200000"
    DTB_ADDR="0x400030100000"

DTB=$PWD/x280.dtb
KERNEL=$PWD/linux/arch/riscv/boot/Image
PAYLOAD=$PWD/opensbi/build/platform/generic/firmware/fw_jump.bin
FS=$PWD/rootfs.ext4
CONSOLE=console/console

python boot.py --boot --opensbi_bin $PAYLOAD --opensbi_dst $PAYLOAD_ADDR --rootfs_bin $FS --rootfs_dst $FS_ADDR --kernel_bin $KERNEL --kernel_dst $KERNEL_ADDR --dtb_bin $DTB --dtb_dst $DTB_ADDR

if [ ! -f "$CONSOLE" ]; then
  pushd console
  make
  popd
fi

$CONSOLE
