# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# Set QUIET=1 in the environment to make the build very quiet.
# Set KBUILD_VERBOSE=1 in the environment to make it very noisy.
ifeq ($(QUIET),1)
    quiet_make := -s
endif

# Default to the pipx python so we get pyluwen for running boot.py
# Set TT_PYTHON in your environment to point to your own venv if you prefer
# TODO: Make this better
ifdef TT_PYTHON
    PYTHON := $(TT_PYTHON)
else
    PYTHON := $(HOME)/.local/share/pipx/venvs/tt-smi/bin/python3
endif

# Use bash as the shell
SHELL := /bin/bash

nproc := $(shell nproc)

# Uncomment this to make the shell rules verbose
# SHELL_VERBOSE := set -x ;

help:
	@echo "Available recipes:"
	@echo "    boot                   # Boot the Blackhole RISC-V CPU"
	@echo "    ttsmi                  # Run tt-smi"
	@echo "    connect                # Connect to console (requires a booted RISC-V)"
	@echo "    build_linux            # Build the kernel"
	@echo "    build_opensbi          # Build opensbi"
	@echo "    build_hosttool         # Build tt-bh-linux"
	@echo "    build_all              # Build everything"
	@echo "    clean_clones           # Clean all cloned trees"
	@echo "    clean_linux            # Clean linux tree and remove binary"
	@echo "    clean_opensbi          # Clean opensbi tree and remove binary"
	@echo "    clean_hosttool         # Clean host tool tree and remove binary"
	@echo "    clean_ttkmd            # Clean tt-kmd tree"
	@echo "    clean_all              # Clean builds and downloads"
	@echo "    clean_builds           # Clean outputs from local builds (not downloads)"
	@echo "    clean_downloads        # Remove all downloaded files"
	@echo "    install_all            # Install all packages"
	@echo "    install_kernel_pkgs    # Install packages needed to build the kernel"
	@echo "    install_toolchain_pkgs # Install packages for cross compiling to riscv64"
	@echo "    install_qemu           # Install riscv qemu and dependencies"
	@echo "    install_tool_pkgs      # Install tools"
	@echo "    install_hosttool_pkgs  # Install libraries for compiling"
	@echo "    install_ttsmi          # Install tt-smi"
	@echo "    install_ttkmd          # Install tt-kmd"
	@echo "    clone_linux            # Clone the Tenstorrent Linux kernel source tree"
	@echo "    clone_opensbi          # Clone the Tenstorrent opensbi source tree"
	@echo "    clone_ttkmd            # Clone the Tenstorrent tt-kmd source tree"
	@echo "    clone_all              # Clone linux, opensbi and tt-kmd trees"
	@echo "    download_rootfs        # Download Ubuntu server 25.04 pre-installed rootfs"
	@echo "    download_prebuilt      # Download prebuilt Linux, opensbi and dtb"
	@echo "    download_all           # Download all preqrequisites"

#################################
# Recipes that run things

# Boot the Blackhole RISC-V CPU
boot: _need_linux _need_opensbi _need_dtb _need_rootfs _need_python _need_ttkmd _need_luwen
	$(PYTHON) boot.py --boot --opensbi_bin fw_jump.bin --opensbi_dst 0x400030000000 --rootfs_bin rootfs.ext4 --rootfs_dst 0x4000e5000000 --kernel_bin Image --kernel_dst 0x400030200000 --dtb_bin blackhole-p100.dtb --dtb_dst 0x400030100000
	./console/tt-bh-linux

# Run tt-smi
ttsmi: _need_ttsmi
	tt-smi

# Connect to console (requires a booted RISC-V)
connect:
	./console/tt-bh-linux

#################################
# Recipes that build things

RV64_TARGETS = build_linux build_opensbi

$(RV64_TARGETS): export CROSS_COMPILE=riscv64-linux-gnu-
$(RV64_TARGETS): export ARCH=riscv

# args: defconfig
define _linux_configure
    @$(SHELL_VERBOSE) \
    set -eo pipefail ; \
    cd linux ; \
    set -x ; \
    $(MAKE) -j $(nproc) $(quiet_make) $(1)
endef

# args: defconfig
define _linux_set_localversion
    @$(SHELL_VERBOSE) \
    set -eo pipefail ; \
    if [ -n "$${CI_JOB_ID:-}" ]; then \
        suffix="ci$$CI_JOB_ID" ; \
    else \
        suffix=$$USER ; \
    fi ; \
    config=$(1) ; \
    localversion="tt-$${config%_*}-$$suffix" ; \
    echo "Setting LOCALVERSION to $$localversion" ; \
    cd linux && ./scripts/config --file .config --set-str LOCALVERSION "-$$localversion"
endef

# Build the kernel
build_linux: _need_riscv64_toolchain _need_gcc _need_linux_tree
	$(call _linux_configure,blackhole_defconfig)
	$(call _linux_set_localversion,blackhole_defconfig)
	$(MAKE) -C linux -j $(nproc) $(quiet_make)
	ln -f linux/arch/riscv/boot/Image Image
	ln -f linux/arch/riscv/boot/dts/tenstorrent/blackhole-p100.dtb blackhole-p100.dtb

# Build opensbi
build_opensbi: _need_riscv64_toolchain _need_gcc _need_opensbi_tree
	$(MAKE) -C opensbi PLATFORM="generic" FW_JUMP="y" FW_JUMP_OFFSET="0x200000" FW_JUMP_FDT_OFFSET="0x100000" BUILD_INFO="y" -j $(nproc) $(quiet_make)
	ln -f opensbi/build/platform/generic/firmware/fw_jump.bin fw_jump.bin

# Build tt-bh-linux
# FIXME needs <slirp/libvdeslirp.h>
build_hosttool: _need_gcc
	$(MAKE) -C console -j $(nproc) $(quiet_make)

# Build everything
build_all: build_linux build_opensbi build_hosttool
	@echo "Build complete! Now run 'make boot' to run Linux"

#################################
# Recipes that clean things

RV64_TARGETS += clean_linux clean_opensbi

# Clean linux tree and remove binary
clean_linux:
	if [ -d linux ]; then $(MAKE) -C linux -j $(nproc) $(quiet_make) clean; fi
	rm -f Image blackhole-p100.dtb

# Clean opensbi tree and remove binary
clean_opensbi:
	if [ -d opensbi ]; then $(MAKE) -C opensbi -j $(nproc) $(quiet_make) clean; fi
	rm -f fw_jump.bin

# Clean tt-kmd tree
clean_ttkmd:
	-if [ -d tt-kmd ]; then $(MAKE) -C tt-kmd -j $(nproc) $(quiet_make) clean; fi

# Clean host tool tree and remove binary
clean_hosttool:
	if [ -d console ]; then $(MAKE) -C console -j $(nproc) $(quiet_make) clean; fi
	rm -f console/tt-bh-linux

# Clean cloned trees
clean_clones:
	rm -rf linux opensbi tt-kmd

# Clean builds and downloads
clean_all: clean_builds clean_downloads clean_clones

# Clean outputs from local builds (not downloads)
clean_builds: clean_linux clean_opensbi clean_hosttool clean_ttkmd

clean: clean_builds

# Remove all downloaded files
clean_downloads:
	rm -f rootfs.ext4

#################################
# Recipes that install packages

# Install all packages
install_all: apt_update install_kernel_pkgs install_toolchain_pkgs install_tool_pkgs install_hosttool_pkgs
	@echo "Install complete! Now run 'make build_all' to build Linux, OpenSBI and the host tool"

sudo := sudo

# args: packages
define install
    $(sudo) DEBIAN_FRONTEND=noninteractive apt-get install -qq --no-install-recommends $(1)
endef

apt_update:
	$(sudo) DEBIAN_FRONTEND=noninteractive apt-get update -qq

# Install packages needed to build the kernel
install_kernel_pkgs:
	$(call install,build-essential libncurses-dev gawk flex bison openssl libssl-dev dkms libelf-dev libudev-dev libpci-dev libiberty-dev autoconf git make bc)

# Install packages for cross compiling to riscv64
install_toolchain_pkgs:
	$(call install,gcc-riscv64-linux-gnu binutils-multiarch ccache)

# Install riscv qemu and dependencies
install_qemu:
	$(call install,qemu-system-misc qemu-utils qemu-system-common qemu-system-data qemu-efi-riscv64)

# Install tools
install_tool_pkgs:
	$(call install,device-tree-compiler xz-utils unzip python3 pipx cargo rustc dkms)

# Install libraries for compiling
install_hosttool_pkgs:
	$(call install,libvdeslirp-dev libslirp-dev)

# Install tt-smi
install_ttsmi: _need_pipx
	pipx install git+https://github.com/tenstorrent/tt-smi
	@echo "Run 'pipx ensurepath' to update your PATH"

# Install tt-kmd
install_ttkmd: _need_dkms _need_ttkmd_tree
	cd tt-kmd && sudo dkms add .
	cd tt-kmd && sudo dkms install tenstorrent/1.34
	cd tt-kmd && sudo modprobe -v tenstorrent

#################################
# Recipes that clone git trees

# args: repo directory branch
define _clone
    $(Q)if [ ! -d $(2) ]; then \
        git clone --depth 1 -b $(3) $(1) $(2); \
    fi
endef

# Clone the Tenstorrent Linux kernel source tree
clone_linux: _need_git
	$(call _clone,https://github.com/tenstorrent/linux,linux,tt-blackhole)

# Clone the Tenstorrent opensbi source tree
clone_opensbi: _need_git
	$(call _clone,https://github.com/tenstorrent/opensbi,opensbi,tt-blackhole)

# Clone the Tenstorrent tt-kmd source tree
clone_ttkmd: _need_git
	$(call _clone,https://github.com/tenstorrent/tt-kmd,tt-kmd,main)

# Clone linux, opensbi and tt-kmd trees
clone_all: clone_linux clone_opensbi clone_ttkmd

#################################
# Recipes that download things

# Download Ubuntu server 25.04 pre-installed rootfs
download_rootfs: _need_wget _need_unxz
	@$(SHELL_VERBOSE) \
	set -eo pipefail; \
	if [ -f rootfs.ext4 ]; then \
		echo "rootfs.ext4 already exists, skipping download."; \
		exit 0; \
	fi; \
	set -x ; \
	wget -O rootfs.ext4 https://github.com/tt-fustini/rootfs/releases/download/v0.1/riscv64.img

# Download prebuilt Linux, opensbi and dtb
download_prebuilt: _need_wget _need_unzip
	# TODO: This is the first passing CI job. It should instead be the latest release
	wget https://github.com/tenstorrent/tt-bh-linux/actions/runs/14748198608/artifacts/3035601544
	unzip tt-bh-linux.zip
	rm tt-bh-linux.zip

download_all: download_rootfs download_prebuilt

#################################
# Helpers

_need_linux:
	$(call _need_file,Image,build,build_linux)

_need_opensbi:
	$(call _need_file,fw_jump.bin,build,build_opensbi)

_need_dtb:
	$(call _need_file,blackhole-p100.dtb,build,build_linux)

_need_rootfs:
	$(call _need_file,rootfs.ext4,build,download_rootfs)

_need_ttkmd:
	$(call _need_file,/dev/tenstorrent/0,install,install_ttkmd)

# The spelling is delibrate as _need_file will add -ing
_need_linux_tree:
	$(call _need_file,linux,clon,clone_linux)

_need_opensbi_tree:
	$(call _need_file,opensbi,clon,clone_opensbi)

_need_ttkmd_tree:
	$(call _need_file,tt-kmd,clon,clone_ttkmd)

_need_dkms:
	$(call _need_prog,dkms,install,install_dkms)

_need_dtc:
	$(call _need_prog,dtc,install,install_tool_pkgs)

_need_gcc:
	$(call _need_prog,gcc,install,install_kernel_pkgs)

_need_git:
	$(call _need_prog,git,install,install_kernel_pkgs)

_need_luwen:
	$(call _need_pylib,pyluwen,install,install_ttsmi)

_need_pipx:
	$(call _need_prog,pipx,install,install_tool_pkgs)

_need_python:
	$(call _need_prog,python3,install,install_tool_pkgs)

_need_riscv64_toolchain:
	$(call _need_prog,riscv64-linux-gnu-gcc,install,install_toolchain_pkgs)

_need_ttsmi:
	$(call _need_prog,tt-smi,install,install_ttsmi)

_need_unxz:
	$(call _need_prog,unxz,install,install_tool_pkgs)

_need_wget:
	$(call _need_prog,wget,install,install_tool_pkgs)

_need_unzip:
	$(call _need_prog,unzip,install,install_tool_pkgs)

# _need_file: Check if a file exists, and if not, run the target to create it
# args: file action-name target
define _need_file =
    @$(SHELL_VERBOSE) \
    if [ -e $(1) ]; then \
        exit 0; \
    fi; \
    if [ "$(2)" = "install" ]; then \
        echo -e '\e[31merror: Missing $(1), install it with \e[32m$(3)\e[0m'; exit 1; \
    fi; \
    if [ "$(2)" = "download" ] && [ "$$MAKE_AUTO_DOWNLOAD" = "0" ]; then \
        echo -e '\e[31merror: Missing $(1), download it with \e[32m$(3)\e[0m'; exit 1; \
    fi; \
    echo -e 'Missing $(1), $(2)ing it with \e[32m$(3)\e[0m'; \
    $(MAKE) $(3)
endef

# _need_prog: Check if a program exists, and if not tell the user how to install it
# args: prog action-name target
define _need_prog =
    @$(SHELL_VERBOSE) \
    if ! which $(1) > /dev/null 2>&1; then \
        echo -e '\e[31merror: Missing $(1), $(2) it with \e[32m$(3)\e[0m'; \
        exit 1; \
    fi
endef

# _need_pylib: Check if a python package exists, and if not tell the user how to install it
# args: python-package action-name target
define _need_pylib =
    @$(SHELL_VERBOSE) \
    if ! echo -e "import sys\ntry:\n\timport $(1)\nexcept ImportError:\n\tsys.exit(1)" | $(PYTHON); then \
        echo -e "\e[31mError: missing python '$(1)', $(2) it with \e[32m$(3)\e[0m"; \
        exit 1; \
    fi
endef

.PHONY: apt_update \
	boot \
	build_all \
	build_hosttool \
	build_linux \
	build_opensbi \
	clean \
	clean_all \
	clean_clones \
	clean_builds \
	clean_downloads \
	clean_hosttool \
	clean_linux \
	clean_opensbi \
	clean_ttkmd \
	clone_all \
	clone_linux \
	clone_opensbi \
	clone_ttkmd \
	connect \
	download_all \
	download_prebuilt \
	download_rootfs \
	help \
	install_all \
	install_hosttool_pkgs \
	install_kernel_pkgs \
	install_qemu \
	install_toolchain_pkgs \
	install_tool_pkgs \
	install_ttkmd \
	install_ttsmi \
	_need_dkms \
	_need_dtb \
	_need_dtc \
	_need_gcc \
	_need_git \
	_need_linux \
	_need_linux_tree \
	_need_luwen \
	_need_opensbi \
	_need_opensbi_tree \
	_need_pipx \
	_need_python \
	_need_riscv64_toolchain \
	_need_rootfs \
	_need_ttkmd \
	_need_ttkmd_tree \
	_need_ttsmi \
	_need_unxz \
	_need_unzip \
	ttsmi
