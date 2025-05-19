## Booting All 4 L2CPUs
Each of the 4 L2CPUs require a different device tree
1. L2CPU0 and 1 have a dedicated 4G DRAM tile each, so can use full 4G memory
1. L2CPU2 and 3 share a 4G DRAM tile, so we must craft each L2CPU's device tree in a manner that they have no overlapping memory regions

We need to make changes to the memory cell, and anything else that uses this memory cell (reserved memory, pmem region, network driver)

`blackhole-p100-2.dts` allocates the first 2G of the memory region for this L2CPU (with appropriate changes to the pmem region)  
`blackhole-p100-3.dts` allocates the last 2G of the memory region for this L2CPU (with appropriate changes to the pmem region)

L2CPU 0 and 1 can use the `blackhole-p100.dts` device tree from the linux kernel tree

To boot all 4 L2CPUs, use the `boot_all` make target

All 4 L2CPUs can boot the same kernel, rootfs, opensbi. We need to provide a different device tree to each and the locations to put these for each L2CPUs

Note: if `blackhole-p100.dts` in the kernel tree changes, the 2 device trees in this folder need to be updated too