# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# Set QUIET=1 in the environment to make the build very quiet.
# Set KBUILD_VERBOSE=1 in the environment to make it very noisy.
quiet_make := if env('QUIET', '') == '1' { '-s' } else { '' }

# Default to the pipx python so we get pyluwen for running boot.py
# Set TT_PYTHON in your environment to point to your own venv if you prefer
python := env('TT_PYTHON', '~/.local/pipx/venvs/tt-smi/bin/python3')

[private]
help:
    @just --list --unsorted --justfile {{justfile()}}

#################################
# Recipes that run things

# Boot the Blackhole RISC-V CPU
boot: _need_linux _need_opensbi _need_dtb _need_rootfs _need_python _need_ttkmd
    {{python}} boot.py --boot --opensbi_bin fw_jump.bin --opensbi_dst 0x400030000000 --rootfs_bin rootfs.ext4 --rootfs_dst 0x4000e5000000 --kernel_bin Image --kernel_dst 0x400030200000 --dtb_bin blackhole-p100.dtb --dtb_dst 0x400030100000
    ./console/tt-bh-linux

# Run tt-smi
ttsmi: _need_ttsmi
    tt-smi

# Connect to console (requires a booted RISC-V)
connect:
    ./console/tt-bh-linux

#################################
# Recipes that build things

_linux_configure defconfig: _need_riscv64_toolchain _need_make _need_linux_tree
    #!/bin/bash
    set -exo pipefail
    export ARCH=riscv
    export CROSS_COMPILE=riscv64-linux-gnu-
    cd linux
    make -j $(nproc) {{quiet_make}} blackhole_defconfig

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
build_linux config='defconfig': (_linux_configure config) (_linux_set_localversion config) _need_riscv64_toolchain _need_make
    cd linux && make ARCH="riscv" CROSS_COMPILE="riscv64-linux-gnu-" -j $(nproc) {{quiet_make}}
    ln -f linux/arch/riscv/boot/Image Image
    ln -f linux/arch/riscv/boot/dts/tenstorrent/blackhole-p100.dtb blackhole-p100.dtb

# Build opensbi
build_opensbi: _need_riscv64_toolchain _need_make _need_opensbi_tree
    cd opensbi && make CROSS_COMPILE="riscv64-linux-gnu-" PLATFORM="generic" FW_JUMP="y" FW_JUMP_OFFSET="0x200000" FW_JUMP_FDT_OFFSET="0x100000" BUILD_INFO="y" -j $(nproc) {{quiet_make}}
    ln -f opensbi/build/platform/generic/firmware/fw_jump.bin fw_jump.bin

# Build tt-bh-linux
build_hosttool: _need_make
	cd console && make -j $(nproc) {{quiet_make}}

# Build everything
build_all: build_linux build_opensbi build_hosttool
    echo "Build complete! Now run 'just boot' to run Linux"

#################################
# Recipes that clean things

# Clean linux tree and remove binary
clean_linux: _need_make
    cd linux && make ARCH="riscv" CROSS_COMPILE="riscv64-linux-gnu-" -j $(nproc) {{quiet_make}} clean
    rm Image

# Clean opensbi tree and remove binary
clean_opensbi: _need_make
    cd opensbi && make -j $(nproc) {{quiet_make}} clean
    rm fw_jump.bin


# Clean host tool tree and remove binary
clean_hosttool: _need_make
    cd console && make -j $(nproc) {{quiet_make}} clean
    rm tt-bh-linux

# Clean builds and downloads
clean_all: clean_builds clean_downloads

# Clean outputs from local builds (not downloads)
clean_builds: clean_linux clean_opensbi clean_hosttool

# Remove all download files
clean_downloads:
	rm rootfs.ext4

#################################
# Recipes that install packages

# Install all packages
install_all: apt_update install_kernel_pkgs install_toolchain_pkgs install_tool_pkgs install_hosttool_pkgs
    echo "Install complete! Now run 'just build_all' to build Linux, OpenSBI and the host tool"

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
install_toolchain_pkgs: (install 'gcc-riscv64-linux-gnu binutils-multiarch ccache')

# Install riscv qemu and dependencies
install_qemu: (install 'qemu-system-misc qemu-utils qemu-system-common qemu-system-data qemu-efi-riscv64')

# Install tools
install_tool_pkgs: (install 'device-tree-compiler xz-utils unzip python3 pipx cargo rustc dkms')

# Install libraries for compiling
install_hosttool_pkgs: (install 'libvdeslirp-dev libslirp-dev')

# Install tt-smi
install_ttsmi: _need_pipx
	pipx install git+https://github.com/tenstorrent/tt-smi
	echo "Run 'pipx ensurepath' to update your PATH"

# Install tt-kmd
install_ttkmd: _need_dkms _need_ttkmd_tree
    cd tt-kmd && sudo dkms add .
    cd tt-kmd && sudo dkms install tenstorrent/1.34
    cd tt-kmd && sudo modprobe -v tenstorrent

#################################
# Recipes that clone git trees

_clone repo branch:
    git clone --depth 1 -b {{branch}} {{repo}}

# Clone the Tenstorrent Linux kernel source tree
clone_linux: (_clone 'https://github.com/tenstorrent/linux' 'tt-blackhole')

# Clone the Tenstorrent opensbi source tree
clone_opensbi: (_clone 'https://github.com/tenstorrent/opensbi' 'tt-blackhole')

# Clone the Tenstorrent tt-kmd source tree
clone_ttkmd: (_clone 'https://github.com/tenstorrent/tt-kmd' 'main')

# Clone linux, opensbi and tt-kmd trees
clone_all: clone_linux clone_opensbi clone_ttkmd

#################################
# Recipes that download things

# Download Ubuntu server 25.04 pre-installed rootfs
download_rootfs: _need_unxz
    #!/bin/bash
    set -exo pipefail
    if [ -f rootfs.ext4 ]; then
        echo "rootfs.ext4 already exists, skipping download."
        exit 0
    fi
    wget -O rootfs.ext4 https://github.com/tt-fustini/rootfs/releases/download/v0.1/riscv64.img

# Download prebuilt Linux, opensbi and dtb
download_prebuilt: _need_unzip
    # TODO: This is the first passing CI job. It should instead be the latest release
    wget https://github.com/tenstorrent/tt-bh-linux/actions/runs/14748198608/artifacts/3035601544
    unzip tt-bh-linux.zip
    rm tt-bh-linux.zip

download_all: download_rootfs download_prebuilt

#################################
# Helpers

_need_linux: (_need_file 'Image' 'build' 'build_linux or "just download_prebuilt"')
_need_opensbi: (_need_file 'fw_jump.bin' 'build' 'build_opensbi or "just download_prebuilt"')
_need_dtb: (_need_file 'blackhole-p100.dtb' 'build' 'build_linux or "just download_prebuilt"')
_need_rootfs: (_need_file 'rootfs.ext4' 'build' 'download_rootfs')

# The spelling is delibrate as _need_file will add -ing
_need_linux_tree: (_need_file 'linux' 'clon' 'clone_linux')
_need_opensbi_tree: (_need_file 'opensbi' 'clon' 'clone_opensbi')
_need_ttkmd_tree:  (_need_file 'tt-kmd' 'clon' 'clone_ttkmd')

_need_make: (_need_prog 'make' 'install' 'install_kernel_pkgs')
_need_dtc: (_need_prog 'dtc' 'install' 'install_tool_pkgs')
_need_unxz: (_need_prog 'unxz' 'install' 'install_tool_pkgs')
_need_unzip: (_need_prog 'unzip' 'install' 'install_tool_pkgs')
_need_riscv64_toolchain: (_need_prog 'riscv64-linux-gnu-gcc' 'install' 'install_toolchain_pkgs')
_need_python: (_need_prog 'python3' 'install' 'install_tool_pkgs')
_need_pipx: (_need_prog 'pipx' 'install' 'install_tool_pkgs')
_need_ttsmi: (_need_prog 'tt-smi' 'install' 'install_ttsmi')
_need_dkms: (_need_prog 'dkms' 'install' 'install_dkms')

_need_ttkmd: (_need_file '/dev/tenstorrent/0' 'install' 'install_ttkmd')

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
