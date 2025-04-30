## Booting All 4 L2CPUs
Each of the 4 L2CPUs require a different device tree
1. L2CPU0 and 1 have a dedicated 4G DRAM tile each, so can use full 4G memory
1. L2CPU2 and 3 share a 4G DRAM tile, so we must craft each L2CPU's device tree in a manner that they have no overlapping memory regions

We need to make changes to the memory cell, and anything else that uses this memory cell (reserved memory, pmem region, network driver)

`x280_l2cpu2.dts` allocates the first 2G of the memory region for this L2CPU (with appropriate changes to the pmem region)  
`x280_l2cpu3.dts` allocates the last 2G of the memory region for this L2CPU (with appropriate changes to the pmem region)

L2CPU 0 and 1 can use the `x280.dts` device tree from the parent folder


To boot all 4 L2CPUs, do the following (can this be put into the justfile @jms?)

```
# Adjusted for FS of size 1200 MB
     FS_ADDR="0x4000e5000000 0x4000e5000000 0x400065000000 0x4000e5000000"
PAYLOAD_ADDR="0x400030000000 0x400030000000 0x400030000000 0x4000B0000000"
 KERNEL_ADDR="0x400030200000 0x400030200000 0x400030200000 0x4000B0200000"
    DTB_ADDR="0x400030100000 0x400030100000 0x400030100000 0x4000B0100000"

PAYLOAD=<path-to-opensbi-fw_jump.bin>
FS=<path-to-rootfs-img>
KERNEL=<path-to-kernel-image>
DTB="./x280.dtb ./x280.dtb ./x280_l2cpu2.dtb ./x280_l2cpu3.dtb"

python boot.py --boot --opensbi_bin $PAYLOAD --opensbi_dst $PAYLOAD_ADDR --rootfs_bin $FS --rootfs_dst $FS_ADDR --kernel_bin $KERNEL --kernel_dst $KERNEL_ADDR --dtb_bin $DTB --dtb_dst $DTB_ADDR
```

All these args must be of the same length (which is the number of L2CPUs that are booted)
--opensbi_dst
--rootfs_dst
--kernel_dst
--dtb_dst
--dtb_bin

Since all 4 L2CPUs can boot the same kernel, rootfs, opensbi, we don't really need to change any of the other args. We just need to provide a different device tree and the locations to put these for each L2CPUs

Note that the --l2cpu arg has been removed now

Someone has to keep these device trees in sync with the one in the parent folder