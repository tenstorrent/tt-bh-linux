![Tenstorrent Blackhole P150](misc/P150.jpg)

# Tenstorrent Blackhole P100/P150 Linux demo

This is a demo of Linux runnning on the [Tenstorrent Blackhole
P100/P150](https://tenstorrent.com/hardware/blackhole) PCIe card using the
onboard [SiFive X280](https://www.sifive.com/cores/intelligence-x200-series)
RISC-V cores.

![alt text](./misc/boot.gif)

(*Note: This is **not** a Tenstorrent designed CPU such as the high performance
[Ascalon](https://tenstorrent.com/en/ip/tt-ascalon) core*)

This demo downloads OpenSBI, Linux and userspace images, configures them on the
Blackhole hardware and starts the X280 cores to boot them.

## Recipe for the Impatient
#### Quick-start to boot Linux to a console, assumes a Debian-based Linux host
Install [tt-kmd](https://github.com/tenstorrent/tt-kmd/) first, either manually
or via [tt-installer](https://github.com/tenstorrent/tt-installer/), then:
```
sudo apt install make
git clone git@github.com:tenstorrent/tt-bh-linux
cd tt-bh-linux
make install_all
make install_ttsmi
make build_all # TODO: make download_all instead? It's currently broken.
make download_rootfs
make boot
```
Log in with user: `root`, password: `root`. Quit with `Ctrl-A x`. Quitting will
leave Linux running. Re-run `console/tt-bh-linux` or `make connect` to
re-establish both console and network connectivity.


## Blackhole Hardware
* Blackhole is a heterogeneous grid of cores linked by a Network on Chip (NOC)
* Core types include Tensix, Ethernet, PCIe, DDR, L2CPU, and more
* Contains 120 Tensix cores (used for ML workloads; not Linux-capable)
* Contains 4x L2CPU blocks
* Each L2CPU contains 4x X280 cores (i.e. 16 cores total; Linux-capable)
* Each L2CPU can run as a single coherent SMP
* Separate L2CPUs aren't coherent
* Memory/registers in a core are accessed through mappable windows in PCIe BARs
  * Software refers to these windows as TLBs
  * Blackhole has 2MB (in BAR0) and 4GB (in BAR4) varieties
  * Allows software running on the host to access arbitrary locations on the NOC
* A similar mappable window mechanism exists in each L2CPU address space
  * Also refered to as TLBs or NOC TLBs
  * 2MB and 128GB varieties
  * Allows software running on X280 to access arbitrary locations on the NOC
* For silicon yield purposes, some cores are disabled
  * Software refers to disabled cores as _harvested_
  * p100a harvesting: 1x DRAM controller (28GB total DRAM); 20x Tensix (120x
total)
  * p150a/b harvesting: DRAM and Tensix are not harvested (32GB total DRAM; 140x
Tensix cores)
  * DRAM controller harvesting on p100a cards affects L2CPU usability (see
diagram)
* The first two L2CPU blocks each have 4GB of DRAM attached; the second two
L2CPU blocks share 4GB of DRAM (see diagram)
```
┌───────────────────────────────────────────────────────────────────────┐
│                                                                       │
│                                                            BLACKHOLE  │
│                                                                       │
│    ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐┌────────┐ ┌────────┐   │
│    │ TENSIX │ │ TENSIX │ │ TENSIX │ │ TENSIX ││ TENSIX │ │ TENSIX │   │
│    └────────┘ └────────┘ └────────┘ └────────┘└────────┘ └────────┘   │
│      ............................................................     │
│    ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐┌────────┐ ┌────────┐   │
│    │ TENSIX │ │ TENSIX │ │ TENSIX │ │ TENSIX ││ TENSIX │ │ TENSIX │   │
│    └────────┘ └────────┘ └────────┘ └────────┘└────────┘ └────────┘   │
│    ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐┌────────┐ ┌────────┐   │
│    │ TENSIX │ │ TENSIX │ │ TENSIX │ │ TENSIX ││ TENSIX │ │ TENSIX │   │
│    └────────┘ └────────┘ └────────┘ └────────┘└────────┘ └────────┘   │
│   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │
│   │ ┌──────┐    │  │ ┌──────┐    │  │ ┌──────┐    │  │ ┌──────┐    │  │
│   │ │┌─────┴┐   │  │ │┌─────┴┐   │  │ │┌─────┴┐   │  │ │┌─────┴┐   │  │
│   │ ││┌─────┴┐  │  │ ││┌─────┴┐  │  │ ││┌─────┴┐  │  │ ││┌─────┴┐  │  │
│   │ │││┌─────┴┐ │  │ │││┌─────┴┐ │  │ │││┌─────┴┐ │  │ │││┌─────┴┐ │  │
│   │ └┤││      │ │  │ └┤││      │ │  │ └┤││      │ │  │ └┤││      │ │  │
│   │  └┤│ X280 │ │  │  └┤│ X280 │ │  │  └┤│ X280 │ │  │  └┤│ X280 │ │  │
│   │   └┤      │ │  │   └┤      │ │  │   └┤      │ │  │   └┤      │ │  │
│   │    └──────┘ │  │    └──────┘ │  │    └──────┘ │  │    └──────┘ │  │
│   │       L2CPU │  │       L2CPU │  │       L2CPU │  │       L2CPU │  │
│   └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  │
│          │                │                └────────┬───────┘         │
└──────────┼────────────────┼─────────────────────────┼─────────────────┘
       ┌───┴───┐        ┌───┴───┐                 ┌───┴───┐
       │       │        │       │                 │       │
       │  4GB  │        │  4GB  │                 │  4GB  │
       │ DRAM  │        │ DRAM  │                 │ DRAM  │
       │       │        │       │                 │       │
       └───────┘        └───────┘                 └───────┘
```
*(Note: This is a simplied diagram. Using memory remapping (via NoC TLBs) all
all L2CPUs can access all DRAMs. Doing this is much slower and potentially
non-coherent)*

Blackhole also has 120 Tensix cores (not Linux capable). P100 has an addtional
16GB of attached DRAM and P150 has an additional 20GB of attached DRAM (ie.
28/32GB for total for P100/P150 respectfully). These additional resources aren't
use for this demo.

This demo *cannot* be used concurrently with the [Tenstorrent AI
stack](https://github.com/tenstorrent/tt-metal).

## Dependencies

The demo uses the same interfaces as the Tenstorrent System Management
Interface (TT-SMI) tool to interact with the Blackhole PCIe device:

 * [luwen](https://github.com/tenstorrent/luwen/), a Rust library with a Python
   interface for programming Tenstorrent hardware
 * [tt-kmd](https://github.com/tenstorrent/tt-kmd/), a Linux kernel module that
   provides an interface for userspace such as luwen

The Makefile can be used to set up tt-smi and tt-kmd automatically.


### Manual installation

If you prefer to manually install tt-smi, first install the dependencies:
```
sudo apt install -y python3-venv cargo rustc
```

Then install tt-smi using pipx:
```
pipx install https://github.com/tenstorrent/tt-smi
```
luwen is installed as a dependency of of tt-smi.

The kernel module can be installed by following the instructions on the
[tt-kmd](https://github.com/tenstorrent/tt-kmd/) repository.

## Theory of Operation
This information is provided as a reference. It is suggested to use `make` to
perform setup and boot steps. Please refer to the Makefile for details on
specific dependencies.

[More information is available here](INFO.md)

### Initial Setup
This is automated by `make build_all` and `make install_ttsmi`.
* Install packages from your Linux distribution:
  * Packages for cross-compiling Linux/OpenSBI for RISC-V
  * Packages for establishing a Python virtual environment for `pyluwen`
  * Build dependencies for the `tt-bh-linux` tool
* Install the tt-kmd kernel driver
* Create a Python virtual environment:
  * Install tt-smi
  * Install pyluwen
* Build a Linux kernel image from
[tt-blackhole branch](https://github.com/tenstorrent/linux/tree/tt-blackhole)
* Build an OpenSBI image from
[tt-blackhole branch](https://github.com/tenstorrent/opensbi/tree/tt-blackhole)
* Build `tt-bh-linux` tool

### Boot Flow
This is automated by `make boot`.
* Enter the Python virtual environment which contains pyluwen
* Invoke boot.py, which:
  * resets the entire Blackhole chip
  * copies binary data to X280's DRAM (OpenSBI, Linux, rootfs, device tree)
  * programs X280 reset vector and resets the L2CPU tile, causing X280 to start
  executing OpenSBI
  * configures X280 L2 prefetcher with parameters recommended by SiFive
* Invoke tt-bh-linux program (establishes console and network access)
* TODO: SSH into the system (How?)

## FAQ

### How does the console work?
* OpenSBI implements a virtual UART consisting of two circular buffers and a
magic signature
* One circular buffer is for transmitting characters; the other is for receiving
* A pointer to this virtual UART exists in a known location
* `bootargs` in the device tree causes Linux to use the hvc driver, which
interfaces with OpenSBI to provide a console to Linux
* Host software (`tt-bh-linux`) maps a 2MB window to the location of OpenSBI in
the X280's memory and scans it to find the pointer to the virtual UART
* `tt-bh-linux` maps a new 2MB window at the location of the virtual UART (which
was discovered in the previous step)
* `tt-bh-linux` and OpenSBI interact with the two circular buffers in a classic
producer/consumer fashion, with `tt-bh-linux` accepting character input from the
user and printing character output from OpenSBI/Linux
* The mechanism is completely polling-driven; there are no interrupts

### How does the network work?
* A [kernel driver](https://github.com/tenstorrent/linux/tree/tt-blackhole/drivers/net/ethernet/tenstorrent)
(on the X280 side) owns TX and RX circular buffers
* `tt-bh-linux` knows the location of these buffers and uses
[libslirp](https://gitlab.freedesktop.org/slirp/libslirp) to provide a host-side
interface
* Via slirp, the host side provides DHCP to the X280 side

### What is the X280's address map?
TODO: are we allowed to share this?

### What is Luwen? pyluwen?
[Luwen]([luwen](https://github.com/tenstorrent/luwen/)) is a library written in
Rust that provides an interface for accessing Tenstorrent accelerator devices.
Luwen provides Python bindings (`pyluwen`), used by boot.py.

### Can the X280 access other cores, e.g. a Tensix or memory on the Linux host?
Yes. Please file an issue if you wish to do this and example code will be
provided.

### Why is `tt-bh-linux`'s console output corrupted?
You probably have multiple instances of it running against one L2CPU.