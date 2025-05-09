![Tenstorrent Blackhole P150](misc/P150.jpg)

# Tenstorrent Blackhole P100/P150 Linux demo

This is a demo of Linux runnning on the [Tenstorrent Blackhole P100/P150](https://tenstorrent.com/hardware/blackhole) PCIe card using the onboard [SiFive x280](https://www.sifive.com/cores/intelligence-x200-series) RISC-V cores.

*FIXME: Replace ASCII art below with boot demo animated gif*
```
 % cat /proc/cpuinfo
 processor     : 0
 hart          : 3
 isa           : rv64imafdcv_zicntr_zicsr_zifencei_zihpm_zaamo_zalrsc_zfh_zca_zcd_zba_zbb_zve32f_zve32x_zve64d_zve64f_zve64x_zvfh_sscofpmf
 mmu           : sv48
 mvendorid     : 0x489
 marchid       : 0x8000000000000007
 mimpid        : 0x6220425
 hart isa      : rv64imafdcv_zicntr_zicsr_zifencei_zihpm_zaamo_zalrsc_zfh_zca_zcd_zba_zbb_zve32f_zve32x_zve64d_zve64f_zve64x_zvfh_sscofpmf
 
 processor     : 1
 hart          : 0
 isa           : rv64imafdcv_zicntr_zicsr_zifencei_zihpm_zaamo_zalrsc_zfh_zca_zcd_zba_zbb_zve32f_zve32x_zve64d_zve64f_zve64x_zvfh_sscofpmf
 mmu           : sv48
 mvendorid     : 0x489
 marchid       : 0x8000000000000007
 mimpid        : 0x6220425
 hart isa      : rv64imafdcv_zicntr_zicsr_zifencei_zihpm_zaamo_zalrsc_zfh_zca_zcd_zba_zbb_zve32f_zve32x_zve64d_zve64f_zve64x_zvfh_sscofpmf
 
 processor     : 2
 hart          : 1
 isa           : rv64imafdcv_zicntr_zicsr_zifencei_zihpm_zaamo_zalrsc_zfh_zca_zcd_zba_zbb_zve32f_zve32x_zve64d_zve64f_zve64x_zvfh_sscofpmf
 mmu           : sv48
 mvendorid     : 0x489
 marchid       : 0x8000000000000007
 mimpid        : 0x6220425
 hart isa      : rv64imafdcv_zicntr_zicsr_zifencei_zihpm_zaamo_zalrsc_zfh_zca_zcd_zba_zbb_zve32f_zve32x_zve64d_zve64f_zve64x_zvfh_sscofpmf
 
 processor     : 3
 hart          : 2
 isa           : rv64imafdcv_zicntr_zicsr_zifencei_zihpm_zaamo_zalrsc_zfh_zca_zcd_zba_zbb_zve32f_zve32x_zve64d_zve64f_zve64x_zvfh_sscofpmf
 mmu           : sv48
 mvendorid     : 0x489
 marchid       : 0x8000000000000007
 mimpid        : 0x6220425
 hart isa      : rv64imafdcv_zicntr_zicsr_zifencei_zihpm_zaamo_zalrsc_zfh_zca_zcd_zba_zbb_zve32f_zve32x_zve64d_zve64f_zve64x_zvfh_sscofpmf
```

(*Note: This is **not** a Tenstorrent designed CPU such as the high performance [Ascalon](https://tenstorrent.com/en/ip/tt-ascalon) core*)

This demo downloads OpenSBI, Linux and userspace images, configures them on the Blackhole hardware and starts the x280 cores to boot them.

## Hardware details
Blackhole has 4 clusters of 4 x280 RISC-V cores called an L2CPU (ie. 16 cores total). Each L2CPU can run as a single coherent SMP. Separate L2CPUs aren't coherent.
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
│   │  └┤│ x280 │ │  │  └┤│ x280 │ │  │  └┤│ x280 │ │  │  └┤│ x280 │ │  │
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
The first two L2CPUs each have 4GB of DRAM attached. The last two share 4GB of DRAM.
*(Note: This is a simplied diagram. Using the memory remapping (via NoC TLBs) all L2CPUs can access all DRAMs. Doing this is much slower and potentially non-coherent)*

Blackhole also has 120 Tensix cores (not Linux capable). P100 has an addtional 16GB of attached DRAM and P150 has an additional 20GB of attached DRAM (ie. 28/32GB for total for P100/P150 respectfully). These additional resources aren't use for this demo.

## Boot Linux

### Dependencies
apt Dependencies for tt-smi and dtc
```
sudo apt install -y python3-venv cargo rustc device-tree-compiler
```

Install tt-smi
```
git clone https://github.com/tenstorrent/tt-smi
python3 -m venv tt-smi/.venv
tt-smi/.venv/bin/pip install ./tt-smi
```

luwen is installed as part of tt-smi

### Before Running Scripts
Activate tt-smi env before running script
```
source tt-smi/.venv/bin/activate
```
Install the `just` package if it exists on your distro, or run the following script:
```
./get_just.sh 
```

### Build Linux and OpenSBI
```
just build_all
```

### Boot Linux
```
./run.sh
```
This will launch a console application.  Default login is `root`/`root`x.  Quit with `Ctrl-A`.

[More information is avalible here](INFO.md)
