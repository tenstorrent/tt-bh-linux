#!/bin/bash

# Adjusted for FS of size 1200 MB
     FS_ADDR="0x4000e5000000"
PAYLOAD_ADDR="0x400030000000"

L2CPU=0
PAYLOAD=$HOME/opensbi/build/platform/generic/firmware/fw_payload.bin
FS=$HOME/rootfs.ext4

tt-smi -r0
python boot.py --boot --l2cpu $L2CPU --opensbi_bin $PAYLOAD --opensbi_dst $PAYLOAD_ADDR --rootfs_bin $FS --rootfs_dst $FS_ADDR
