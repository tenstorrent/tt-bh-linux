![Tenstorrent Blackhole P150](misc/P150.jpg)

# Tenstorrent Blackhole P100/P150 Linux demo

This is a demo of Linux runnning on the [Tenstorrent Blackhole P100/P150](https://tenstorrent.com/hardware/blackhole) PCIe card using the onboard [SiFive x280](https://www.sifive.com/cores/intelligence-x200-series) cores.

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

This demo downloads OpenSBI and Linux binaries, configures them on the Blackhole hardware and
starts the x280 core to boot.

## Hardware details
Blackhole has 4 clusters of 4 x280 cores (ie 16 cores total). Each cluster can run as a single coherent SMP. Separate clusters aren't coherent. Each cluster has 4GB of memory attached.

*FIXME: More details*

## Boot flow details

*FIXME: More details*

## Build and boot Linux

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
This will launch a console application.  Default login is `root`/`root`.  Quit with `Ctrl-A`.


## Additional information

### Device Tree
Sample device tree provided at `x280.dts`. This uses 2896MB of memory for the
host and the remaining 1200MB for the rootfs. The values in the next section
hence assume we're putting the rootfs at 0x4000e5000000 (which is
0x400030000000 + 2896MB)

Compiling device tree `dtc x280.dts > x280.dtb`

### Different methods of booting
1. Running with FW_PAYLOAD, Opensbi has kernel and dtb integrated into it
Opensbi compiled with these args
```
CROSS_COMPILE=riscv64-linux-gnu- make DEBUG=1 FW_PIC=y FW_PAYLOAD_OFFSET=0x200000 PLATFORM=generic FW_PAYLOAD=y FW_PAYLOAD_PATH=<path-to-kernel> FW_FDT_PATH=<path-to-dtb>
```

```
# Adjusted for FS of size 1200 MB
     FS_ADDR="0x4000e5000000"
PAYLOAD_ADDR="0x400030000000"

PAYLOAD=<path-to-opensbi-fw_payload.bin>
FS=<path-to-rootfs-img>

python boot.py --boot --opensbi_bin $PAYLOAD --opensbi_dst $PAYLOAD_ADDR --rootfs_bin $FS --rootfs_dst $FS_ADDR
```

2. Running with FW_JUMP, Opensbi has dtb integrated into it, kernel is separate
Opensbi compiled with these args
```
CROSS_COMPILE=riscv64-linux-gnu- make DEBUG=1 FW_PIC=y FW_JUMP=y FW_JUMP_OFFSET=0x200000 FW_FDT_PATH=<path-to-dtb> PLATFORM=generic
```

```
# Adjusted for FS of size 1200 MB
     FS_ADDR="0x4000e5000000"
PAYLOAD_ADDR="0x400030000000"
 KERNEL_ADDR="0x400030200000"

PAYLOAD=<path-to-opensbi-fw_jump.bin>
FS=<path-to-rootfs-img>
KERNEL=<path-to-kernel-image>

python boot.py --boot --opensbi_bin $PAYLOAD --opensbi_dst $PAYLOAD_ADDR --rootfs_bin $FS --rootfs_dst $FS_ADDR --kernel_bin $KERNEL --kernel_dst $KERNEL_ADDR
```

3. Running with FW_JUMP, Opensbi doesn't have kernel or dtb integrated, both are separate
Note: this doesn't work currently, requires some patching to opensbi to set dtb
location in a1 register before boot (@mikey)

Opensbi compiled with these args
```
CROSS_COMPILE=riscv64-linux-gnu- make DEBUG=1 FW_PIC=y FW_JUMP=y FW_JUMP_OFFSET=0x200000 FW_JUMP_FDT_OFFSET=0x100000  PLATFORM=generic
```

```
# Adjusted for FS of size 1200 MB
     FS_ADDR="0x4000e5000000"
PAYLOAD_ADDR="0x400030000000"
 KERNEL_ADDR="0x400030200000"
    DTB_ADDR="0x400030100000"

PAYLOAD=<path-to-opensbi-fw_jump.bin>
FS=<path-to-rootfs-img>
KERNEL=<path-to-kernel-image>
DTB=<path-to-dtb>

python boot.py --boot --opensbi_bin $PAYLOAD --opensbi_dst $PAYLOAD_ADDR --rootfs_bin $FS --rootfs_dst $FS_ADDR --kernel_bin $KERNEL --kernel_dst $KERNEL_ADDR --dtb_bin $DTB --dtb_dst $DTB_ADDR 

```

4. [TODO] Write an example on putting FS in a remote DRAM tile so that x280
   gets full 4G of memory to itself
