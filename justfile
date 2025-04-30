# Set QUIET=1 in the environment to make the build very quiet.
# Set KBUILD_VERBOSE=1 in the environment to make it very noisy.
quiet_make := if env('QUIET', '') == '1' { '-s' } else { '' }

[private]
help:
    @just --list --unsorted --justfile {{justfile()}}

# Clean builds and downloads
clean_all: clean_builds clean_downloads

# Clean outputs from local builds (not downloads)
clean_builds:

# Remove all download files (except the toolchain)
clean_downloads:

#################################
# Recipes that run things

boot: _need_linux _need_opensbi _need_dtb _need_rootfs _need_python
    python3 boot.py --boot --l2cpu 0 --opensbi_bin fw_jump.bin --opensbi_dst 0x400030000000 --rootfs_bin rootfs.ext4 --rootfs_dst 0x4000e5000000 --kernel_bin Image --kernel_dst 0x400030200000 --dtb_bin 0x400030100000 --dtb_dst 0x400030100000 

#################################
# Recipes that build things

_linux_configure defconfig: _need_toolchain _need_make _need_linux_tree
    cd linux && make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- {{quiet_make}} {{defconfig}}

_linux_set_localversion defconfig:
    #!/bin/bash
    if [ -n "${CI_JOB_ID:-}" ]; then
        suffix="ci$CI_JOB_ID";
    else
        suffix=$USER;
    fi
    # take the bit before _defconfig
    config={{defconfig}}
    localversion="tt-${config%_*}-$suffix"
    echo "Setting LOCALVERSION to $localversion"
    cd linux && ./scripts/config --file .config --set-str LOCALVERSION "-$localversion"

# Build the kernel
build_linux config='defconfig': (_linux_configure config) (_linux_set_localversion config) _need_toolchain _need_make
    cd linux && make ARCH="riscv" CROSS_COMPILE="riscv64-linux-gnu-" -j $(nproc) {{quiet_make}}
    ln -f linux/arch/riscv/boot/Image Image

# Build opensbi
build_opensbi: _need_toolchain _need_make _need_opensbi_tree
    cd opensbi && make CROSS_COMPILE="riscv64-linux-gnu-" PLATFORM="generic" FW_JUMP="y" FW_JUMP_OFFSET="0x200000" FW_JUMP_FDT_OFFSET="0x100000" BUILD_INFO="y" -j $(nproc) {{quiet_make}}
    ln -f opensbi/build/platform/generic/firmware/fw_jump.bin fw_jump.bin

# Build device tree blob
build_dtb dt='x280': _need_dtc
	dtc -I dts -O dtb {{dt}}.dts -o {{dt}}.dtb

# Build rootfs
build_rootfs: _need_unxz
    wget https://cdimage.ubuntu.com/releases/25.04/release/ubuntu-25.04-preinstalled-server-riscv64.img.xz
    unxz ubuntu-25.04-preinstalled-server-riscv64.img.xz
    mv ubuntu-25.04-preinstalled-server-riscv64.img rootfs.ext4

# Build everything
build_all: build_linux build_opensbi build_dtb

#################################
# Recipes that install packages

# Install all packages
install_all: apt_update install_kernel_pkgs install_toolchain_pkgs install_tool_pkgs

sudo := 'sudo'

[private]
install packages:
    {{sudo}} DEBIAN_FRONTEND=noninteractive apt-get install -qq --no-install-recommends {{packages}}

[private]
apt_update:
    {{sudo}} DEBIAN_FRONTEND=noninteractive apt-get update -qq

# Install packages needed to build the kernel
install_kernel_pkgs: (install 'build-essential libncurses-dev gawk flex bison openssl libssl-dev dkms libelf-dev libudev-dev libpci-dev libiberty-dev autoconf git make bc')

# Install packages for cross compiling to riscv64 
install_toolchain_pkgs: (install 'gcc-riscv64-linux-gnu g++-riscv64-linux-gnu binutils-multiarch')

# Install riscv qemu and dependencies
install_qemu: (install 'qemu-system-misc qemu-utils qemu-system-common qemu-system-data qemu-efi-riscv64')

# Install tools
install_tool_pkgs: (install 'device-tree-compiler xz-utils python3')

#################################
# Recipes that download things

_clone repo:
    git clone --depth 1 {{repo}}

# Clone the Linux kernel source tree
clone_kernel: (_clone 'https://github.com/tenstorrent/linux')

# Clone the Tenstorernt opensbi source tree
clone_opensbi: (_clone 'https://github.com/riscv-software-src/opensbi')

# Clone linux and opensbi trees
clone_all: clone_kernel clone_opensbi

_need_linux: (_need_file 'Image' 'build' 'build_linux')
_need_opensbi: (_need_file 'fw_jump.bin' 'build' 'build_opensbi')
_need_dtb: (_need_file 'x280.dtb' 'build' 'build_dtb')
_need_rootfs: (_need_file 'rootfs.ext4' 'build' 'build_rootfs')

_need_linux_tree: (_need_file 'linux' 'clone' 'clone_linux') 
_need_opensbi_tree: (_need_file 'opensbi' 'clone' 'clone_opensbi') 

_need_make: (_need_prog 'make' 'install' 'install_kernel_pkgs')
_need_dtc: (_need_prog 'dtc' 'install' 'install_tool_pkgs')
_need_unxz: (_need_prog 'unxz' 'install' 'install_tool_pkgs')
_need_toolchain: (_need_prog 'riscv64-linux-gnu-gcc' 'install' 'install_toolchain_pkgs')
_need_python: (_need_prog 'python3' 'install' 'install_tool_pkgs')

[no-exit-message]
_need_file path action target *params:
    #!/bin/bash
    if [ -e {{path}} ]; then
        exit 0
    fi

    if [[ "{{action}}" == "install" ]]; then
        echo -e '\e[31merror: Missing {{path}}, install it with \e[32m{{target}}\e[0m';
        exit 1
    fi

    if [[ "{{action}}" == "download" && "$JUST_AUTO_DOWNLOAD" == "0" ]]; then
        echo -e '\e[31merror: Missing {{path}}, download it with \e[32m{{target}}\e[0m';
        exit 1
    fi

    echo -e 'Missing {{path}}, {{action}}ing it with \e[32m{{target}}\e[0m';
    just {{target}} {{params}}

[no-exit-message]
_need_prog prog action target:
    #!/bin/bash
    if [ ! $(which {{prog}}) ]; then
        echo -e '\e[31merror: Missing {{prog}}, {{action}} it with \e[32m{{target}}\e[0m';
        exit 1
    fi
