# X280 and NOC addressing

This document describes the mechanisms for two use cases:

* X280 access of DRAM via the NOC, i.e. is not AXI-attached at 0x4000’3000’0000.
* X280 access of host memory.

 

### X280 to NOC TLBs
* X280 has 2MB and 128GB windows that can be mapped to NOC endpoints.
* There are 224 2MB windows and 32 128GB windows.
* These windows appear in both the System Port and the Memory Port regions of
the X280 address space.
* Access to the NOC via System Port is uncached by the X280
* Access to the NOC via Memory Port is cached by the X280
* When programming a window, the destination address must be aligned to the size
of the window because the low N bits are discarded when programming the TLB
registers. For example, if you are trying to perform a 32-bit read of (x=0, y=0,
addr=0x20000C) through a 2MB window:
  * 0x20000C >> 21 == 0x1.  Program this into the TLB register’s address field.
  * 0xC is the offset into the window to read.
* Use x_end and y_end to access a single NOC endpoint (unicast).

### Example code: X280 TLB registers, windows
```
#define SYSTEM_PORT 0x30000000UL

// Sizes, addresses, quantities of the NOC windows.
#define WINDOW_2M_COUNT 224
#define WINDOW_2M_SHIFT 21
#define WINDOW_2M_SIZE (1 << WINDOW_2M_SHIFT)
#define WINDOW_2M_BASE (SYSTEM_PORT + 0x400000000UL)
#define WINDOW_2M_ADDR(n) (WINDOW_2M_BASE + (WINDOW_2M_SIZE * n))
#define WINDOW_128G_COUNT 32
#define WINDOW_128G_SHIFT 37
#define WINDOW_128G_SIZE (1UL << WINDOW_128G_SHIFT)
#define WINDOW_128G_BASE (SYSTEM_PORT + 0x80400000000)
#define WINDOW_128G_ADDR(n) (WINDOW_128G_BASE + (WINDOW_128G_SIZE * (n)))

// TLB registers control NOC window configuration.
#define TLB_2M_CONFIG_BASE 0x2ff00000
#define TLB_2M_CONFIG_SIZE (0x10 * WINDOW_2M_COUNT)
#define TLB_128G_CONFIG_BASE (TLB_2M_CONFIG_BASE + TLB_2M_CONFIG_SIZE)
#define TLB_128G_CONFIG_SIZE (0x0c * WINDOW_128G_COUNT)

/**
 * struct TLB_2M_REG - TLB register layout for 2M windows
 */
struct TLB_2M_REG {
	union {
		struct {
			u32 data[4];
		};
		struct {
			u64 addr : 43;
			u64 reserved0 : 21;
			u64 x_end : 6;
			u64 y_end : 6;
			u64 x_start : 6;
			u64 y_start : 6;
			u64 multicast_en : 1;
			u64 strict_order : 1;
			u64 posted : 1;
			u64 linked : 1;
			u64 static_en : 1;
			u64 stream_header : 1;
			u64 reserved1 : 1;
			u64 noc_selector : 1;
			u64 static_vc : 3;
			u64 strided : 8;
			u64 exclude_coord_x : 5;
			u64 exclude_coord_y : 4;
			u64 exclude_dir_x : 1;
			u64 exclude_dir_y : 1;
			u64 exclude_enable : 1;
			u64 exclude_routing_option : 1;
			u64 num_destinations : 8;
		};
	};
};
/**
 * struct TLB_128G_REG - TLB register layout for 128G windows
 */
struct TLB_128G_REG {
	union {
		struct {
			u32 data[3];
		};
		struct {
			u64 addr : 27;
			u64 reserved0 : 5;
			u64 x_end : 6;
			u64 y_end : 6;
			u64 x_start : 6;
			u64 y_start : 6;
			u64 multicast_en : 1;
			u64 strict_order : 1;
			u64 posted : 1;
			u64 linked : 1;
			u64 static_en : 1;
			u64 stream_header : 1;
			u64 reserved1 : 1;
			u64 noc_selector : 1;
			u64 static_vc : 3;
			u64 strided : 8;
			u64 exclude_coord_x : 5;
			u64 exclude_coord_y : 4;
			u64 exclude_dir_x : 1;
			u64 exclude_dir_y : 1;
			u64 exclude_enable : 1;
			u64 exclude_routing_option : 1;
			u64 num_destinations : 8;
		};
	};
};
```

### X280, Linux access to ‘remote’ DRAM
* Configure a 128GB TLB window to map to the DRAM’s (x, y, addr=0x0).
* Add a pmem device tree entry using the physical address of the X280->NOC
window and appropriate size.

 

### X280, Linux access to host memory
* Enable system IOMMU.
* Allocate a page-aligned buffer of the desired size.
* Use tt-kmd’s PIN_PAGES API to map this buffer for DMA access by the device.
* Problem: the resulting IOVA is not predictable.
    * NOC access to the memory is possible at (x=PCIE_X, y=PCIE_Y, addr=IOVA),
    assuming the IOVA is less than 58 bits (it should be).
    * Mapping a 128GB TLB from X280 to (x=PCIE_X, y=PCIE_Y, addr=IOVA) is probably
not possible (it is unlikely that IOVA will be aligned to a 128GB boundary).
* Solution: use PCIe iATU.
    * Map a segment of the 58-bit NOC address space at (x=PCIE_X, y=PCIE_Y,
    addr=0x1000000000000000) to the IOVA.
    * Map X280 TLB window at that NOC address.
* Add a pmem device tree entry using the physical address of the X280->NOC
window and appropriate size.
* On Blackhole products (p100a, p150a, p150b) use (x=19, y=24) to access
the PCIe endpoint tile via NOC.  This coodinate is valid for both NOC0 and NOC1.
Please note: this may not be valid for pre-production hardware without hardware
translation enabled.

Two caveats:
1. This doesn’t work with a mmaped file.
2. tt-kmd ownership of iATU programming is a work in process with a goal of
preventing different userspace applications from running over each other’s iATU
regions.  The use-case described above is not supported by tt-kmd but could be.
PIN_PAGES supports iATU programming for AI and system tools use cases, but not
this.  Design proposal for adding this support: add a flag to PIN_PAGES API to
request an iATU region with 128GB alignment, far enough away from the region
used by application software.