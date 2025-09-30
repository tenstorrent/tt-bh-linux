# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# Set QUIET=1 in the environment to make the build very quiet.
# Set KBUILD_VERBOSE=1 in the environment to make it very noisy.
ifeq ($(QUIET),1)
    quiet_make := -s
endif

# Default to python env installed by tt-installer
# If it doesn't exist, use python in current venv
ifneq ($(wildcard $(HOME)/.tenstorrent-venv/bin/python),)
	PYTHON := $(HOME)/.tenstorrent-venv/bin/python
else
	PYTHON := python3
endif

# Default riscv64 disk image file. Change this to point at your local image
# if you have one
DISK_IMAGE := rootfs.ext4

L2CPU ?= 0

# Use bash as the shell
SHELL := /bin/bash

nproc := $(shell nproc)

# Uncomment this to make the shell rules verbose
# SHELL_VERBOSE := set -x ;

help:
	@echo "      ______                __                             __"
	@echo "     /_  __/__  ____  _____/ /_____  _____________  ____  / /_"
	@echo "      / / / _ \/ __ \/ ___/ __/ __ \/ ___/ ___/ _ \/ __ \/ __/"
	@echo "     / / /  __/ / / (__  ) /_/ /_/ / /  / /  /  __/ / / / /_"
	@echo "    /_/  \___/_/ /_/____/\__/\____/_/  /_/   \___/_/ /_/\__/"
	@echo "                     Blackhole Linux Demo"
	@echo ""
	@echo "Quick start with pre-built binaries:"
	@echo "    install_tt_installer   # Install host kernel module and tools"
	@echo "    download_all           # Download pre-built rootfs, firmware and kernel"
	@echo "    install_all            # Install dependancies for compiling host tool"
	@echo "    boot                   # Boot the Blackhole RISC-V CPU and connect console"
	@echo ""
	@echo "See README.md for more information, or use the recipies below to experiment."
	@echo ""
	@echo "Available recipes:"
	@echo "    boot                   # Boot the Blackhole RISC-V CPU"
	@echo "    connect                # Connect to console (requires a booted RISC-V)"
	@echo "    ssh                    # SSH to machine (requires a booted RISC-V)"
	@echo "    boot_all               # Boot all 4 L2CPUs on Blackhole Chip"
	@echo "    connect_all            # Launch 4x4 Tmux grid and connect to each L2CPU"
	@echo "    build_linux            # Build the kernel"
	@echo "    build_opensbi          # Build opensbi"
	@echo "    build_hosttool         # Build tt-bh-linux"
	@echo "    build_all              # Build everything"
	@echo "    clean_clones           # Clean all cloned trees"
	@echo "    clean_linux            # Clean linux tree and remove binary"
	@echo "    clean_opensbi          # Clean opensbi tree and remove binary"
	@echo "    clean_hosttool         # Clean host tool tree and remove binary"
	@echo "    clean_all              # Clean builds and downloads"
	@echo "    clean_builds           # Clean outputs from local builds (not downloads)"
	@echo "    clean_downloads        # Remove all downloaded files"
	@echo "    install_all            # Install all packages"
	@echo "    install_kernel_pkgs    # Install packages needed to build the kernel"
	@echo "    install_qemu           # Install riscv qemu and dependencies"
	@echo "    install_hosttool_pkgs  # Install dependancies for compiling host tool"
	@echo "    install_tt_installer   # Install (run) tt-installer for tt-kmd, tt-smi and luwen"
	@echo "    clone_linux            # Clone the Tenstorrent Linux kernel source tree"
	@echo "    clone_opensbi          # Clone the Tenstorrent opensbi source tree"
	@echo "    clone_all              # Clone linux and opensbi trees"
	@echo "    download_rootfs        # Download Debian Trixie riscv64 rootfs"
	@echo "    download_prebuilt      # Download prebuilt Linux, opensbi and dtb"
	@echo "    download_all           # Download all preqrequisites"

#################################
# Recipes that run things

# Boot one L2CPU in Blackhole RISC-V CPU
boot: _need_linux _need_opensbi _need_dtb _need_rootfs _need_hosttool _need_python _need_luwen _need_ttkmd
	$(PYTHON) boot.py --boot --l2cpu $(L2CPU) --opensbi_bin fw_jump.bin --opensbi_dst 0x400030000000 --rootfs_bin $(DISK_IMAGE) --rootfs_dst 0x4000e5000000 --kernel_bin Image --kernel_dst 0x400030200000 --dtb_bin blackhole-card.dtb --dtb_dst 0x400030100000
	./console/tt-bh-linux --l2cpu $(L2CPU)

boot_all: _need_linux _need_opensbi _need_dtb _need_dtb_all _need_rootfs _need_hosttool _need_python _need_luwen _need_ttkmd
	$(PYTHON) boot.py --boot --l2cpu 0 1 2 3 --opensbi_bin fw_jump.bin --opensbi_dst 0x400030000000 0x400030000000 0x400030000000 0x4000b0000000 --rootfs_bin $(DISK_IMAGE) --rootfs_dst 0x4000e5000000 0x4000e5000000 0x400065000000 0x4000e5000000  --kernel_bin Image --kernel_dst 0x400030200000 0x400030200000 0x400030200000 0x4000b0200000 --dtb_bin blackhole-card.dtb blackhole-card.dtb blackhole-card-2.dtb blackhole-card-3.dtb --dtb_dst 0x400030100000 0x400030100000 0x400030100000 0x4000b0100000

# Connect to console (requires a booted RISC-V)
connect: _need_hosttool _need_ttkmd
	./console/tt-bh-linux --l2cpu $(L2CPU)

# Connect over SSH (requires a booted RISC-V)
ssh:
	ssh -F /dev/null -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o NoHostAuthenticationForLocalhost=yes -o User=debian -p2222 localhost

SESSION = connect_all
# Launch tmux with a 2x2 grid and connect to each l2cpu in each
connect_all: _need_tmux
	# Kill any existing sessions named connect_all
	tmux has-session -t "$(SESSION)" 2>/dev/null && tmux kill-session -t "$(SESSION)" || true

	tmux new-session  -d -s "$(SESSION)" './console/tt-bh-linux --l2cpu 0' 	# pane 0
	tmux split-window -h -t "$(SESSION)":0 './console/tt-bh-linux --l2cpu 1' 	# pane 1 (right)
	tmux select-pane   -t "$(SESSION)":0.0 									# back to pane 0
	tmux split-window -v -t "$(SESSION)":0 './console/tt-bh-linux --l2cpu 2' 	# pane 2 (bottom-left)
	tmux select-pane   -t "$(SESSION)":0.1 									# go to pane 1
	tmux split-window -v -t "$(SESSION)":0 './console/tt-bh-linux --l2cpu 3' 	# pane 3 (bottom-right)
	tmux select-layout -t "$(SESSION)":0 tiled 								# ensure 2x2 grid

	# If we're already inside a tmux session, we need to use switch-client
	if [ -n "$$TMUX" ]; then \
        tmux switch-client -t "$(SESSION)"; \
    else \
        tmux attach-session -t "$(SESSION)"; \
    fi


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
build_linux: _need_riscv64_toolchain _need_gcc _need_dtc _need_linux_tree
	cp blackhole_defconfig linux/arch/riscv/configs/
	$(call _linux_configure,blackhole_defconfig)
	$(call _linux_set_localversion,blackhole_defconfig)
	$(MAKE) -C linux W=1 -j $(nproc) $(quiet_make)
	$(MAKE) -C linux W=1 -j $(nproc) $(quiet_make) dtbs_check
	ln -f linux/arch/riscv/boot/Image Image
	ln -f linux/arch/riscv/boot/dts/tenstorrent/blackhole-card.dtb blackhole-card.dtb

# Build opensbi
build_opensbi: _need_riscv64_toolchain _need_gcc _need_python _need_opensbi_tree
	$(MAKE) -C opensbi PLATFORM="generic" FW_JUMP="y" FW_JUMP_OFFSET="0x200000" FW_JUMP_FDT_OFFSET="0x100000" BUILD_INFO="y" -j $(nproc) $(quiet_make)
	ln -f opensbi/build/platform/generic/firmware/fw_jump.bin fw_jump.bin

# Build tt-bh-linux
build_hosttool: _need_gcc _need_libvdevslirp
	$(MAKE) -C console -j $(nproc) $(quiet_make)

# Generate a SSH key and add it to the image
build_ssh_key: _need_e2tools
	if [ ! -e user ]; then ssh-keygen -f user -N ''; fi
	e2mkdir -G 1000 -O 1000 -P 755 $(DISK_IMAGE):/home/debian/.ssh
	e2cp -G 1000 -O 1000 -P 600 user.pub $(DISK_IMAGE):/home/debian/.ssh/authorized_keys

# Build everything
build_all: build_linux build_opensbi build_hosttool
	@echo "Build complete! Now run 'make boot' to run Linux"

build_dtb_all:
	dtc misc/blackhole-card-2.dts > blackhole-card-2.dtb
	dtc misc/blackhole-card-3.dts > blackhole-card-3.dtb

#################################
# Recipes that clean things

RV64_TARGETS += clean_linux clean_opensbi

# Clean linux tree and remove binary
clean_linux:
	if [ -d linux ]; then $(MAKE) -C linux -j $(nproc) $(quiet_make) clean; fi
	rm -f Image blackhole-card.dtb

# Clean opensbi tree and remove binary
clean_opensbi:
	if [ -d opensbi ]; then $(MAKE) -C opensbi -j $(nproc) $(quiet_make) clean; fi
	rm -f fw_jump.bin

# Clean host tool tree and remove binary
clean_hosttool:
	if [ -d console ]; then $(MAKE) -C console -j $(nproc) $(quiet_make) clean; fi
	rm -f console/tt-bh-linux

# Clean cloned trees
clean_clones:
	rm -rf linux opensbi

# Clean builds and downloads
clean_all: clean_builds clean_downloads clean_clones

# Clean outputs from local builds (not downloads)
clean_builds: clean_linux clean_opensbi clean_hosttool

clean: clean_builds

# Remove all downloaded files
clean_downloads:
	rm -f $(DISK_IMAGE) tt-bh-disk-image.zip tt-bh-linux.zip tt-installer-v1.1.0.sh

#################################
# Recipes that install packages

# Install all packages
install_all: apt_update install_kernel_pkgs install_hosttool_pkgs
	@echo "Install complete! Now run 'make build_all' to build Linux, OpenSBI and the host tool"

sudo := sudo

# args: packages
define install
    $(sudo) DEBIAN_FRONTEND=noninteractive apt-get install -qq --no-install-recommends $(1)
endef

apt_update:
	$(sudo) DEBIAN_FRONTEND=noninteractive apt-get update -qq

# Install packages needed to build the kernel and opensbi for riscv64. These
# are not required if using prebuilt images.
install_kernel_pkgs:
	$(call install,build-essential libncurses-dev gawk flex bison openssl libssl-dev libelf-dev libudev-dev libpci-dev libiberty-dev autoconf git make bc gcc-riscv64-linux-gnu binutils-multiarch ccache device-tree-compiler)

# Install riscv qemu and dependencies
install_qemu:
	$(call install,qemu-system-misc qemu-utils qemu-system-common qemu-system-data qemu-efi-riscv64)

# Install libraries for compiling the host tool and modifying disk images
install_hosttool_pkgs:
	$(call install,libvdeslirp-dev libslirp-dev xz-utils unzip e2tools tmux)

install_tt_installer: _need_tt_installer
	TT_MODE_NON_INTERACTIVE=0 TT_SKIP_INSTALL_HUGEPAGES=0 TT_SKIP_UPDATE_FIRMWARE=0 TT_SKIP_INSTALL_PODMAN=0 TT_SKIP_INSTALL_METALLIUM_CONTAINER=0 TT_REBOOT_OPTION=2 ./tt-installer-v1.1.0.sh

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
	$(call _clone,https://github.com/tenstorrent/linux,linux,b4/tt-bh-dts)

# Clone the Tenstorrent opensbi source tree
clone_opensbi: _need_git
	$(call _clone,https://github.com/tenstorrent/opensbi,opensbi,tt-blackhole)

# Clone linux and opensbi trees
clone_all: clone_linux clone_opensbi

#################################
# Recipes that download things

define wget
    wget -O $(1).tmp $(2)
    mv -f $(1).tmp $(1)
endef

# Download Debian Trixie riscv64 rootfs
download_rootfs: _need_wget _need_unxz
	@$(SHELL_VERBOSE) \
	set -eo pipefail; \
	if [ -f $(DISK_IMAGE) ]; then \
		echo "$(DISK_IMAGE) already exists, skipping download."; \
		exit 0; \
	fi; \
	set -x ; \
	$(call wget,tt-bh-disk-image.zip,https://github.com/tenstorrent/tt-bh-linux/releases/download/v0.2/tt-bh-disk-image.zip)
	unzip tt-bh-disk-image.zip
	rm tt-bh-disk-image.zip
	mv debian-riscv64.img rootfs.ext4

# Download prebuilt Linux, opensbi and dtb
download_prebuilt: _need_wget _need_unzip
	# TODO: Test this once repo is public
	$(call wget,tt-bh-linux.zip,https://github.com/tenstorrent/tt-bh-linux/releases/download/v0.2/tt-bh-linux.zip)
	unzip tt-bh-linux.zip
	rm tt-bh-linux.zip

download_tt_installer:
	$(call wget,tt-installer-v1.1.0.sh,https://github.com/tenstorrent/tt-installer/releases/download/v1.1.0/install.sh)
	chmod u+x tt-installer-v1.1.0.sh

download_all: download_rootfs download_prebuilt

#################################
# Helpers

_need_linux:
	$(call _need_file,Image,build,build_linux)

_need_opensbi:
	$(call _need_file,fw_jump.bin,build,build_opensbi)

_need_dtb:
	$(call _need_file,blackhole-card.dtb,build,build_linux)

_need_dtb_all:
	$(call _need_file,blackhole-card-2.dtb,build,build_dtb_all)
	$(call _need_file,blackhole-card-3.dtb,build,build_dtb_all)

_need_rootfs:
	$(call _need_file,$(DISK_IMAGE),build,download_rootfs)

_need_hosttool:
	$(call _need_file,console/tt-bh-linux,build,build_hosttool)

_need_ttkmd:
	$(call _need_file,/dev/tenstorrent/0,install,install_ttkmd)

# The spelling is delibrate as _need_file will add -ing
_need_linux_tree:
	$(call _need_file,linux,clon,clone_linux)

_need_opensbi_tree:
	$(call _need_file,opensbi,clon,clone_opensbi)

_need_dtc:
	$(call _need_prog,dtc,install,install_tool_pkgs)

_need_gcc:
	$(call _need_prog,gcc,install,install_kernel_pkgs)

_need_git:
	$(call _need_prog,git,install,install_kernel_pkgs)

_need_luwen:
	$(call _need_pylib,pyluwen,install,install_tt_installer)

_need_python:
	$(call _need_prog,python3,install,install_tool_pkgs)

_need_riscv64_toolchain:
	$(call _need_prog,riscv64-linux-gnu-gcc,install,install_toolchain_pkgs)

_need_unxz:
	$(call _need_prog,unxz,install,install_tool_pkgs)

_need_wget:
	$(call _need_prog,wget,install,install_tool_pkgs)

_need_unzip:
	$(call _need_prog,unzip,install,install_tool_pkgs)

_need_e2tools:
	$(call _need_prog,e2cp,install,install_tool_pkgs)

_need_ssh_key:
	$(call _need_file,user,build_ssh_key)

_need_tt_installer:
	$(call _need_file,tt-installer-v1.1.0.sh,download,download_tt_installer)

_need_tmux:
	$(call _need_prog,tmux,install,install_hosttool_pkgs)

_need_libvdevslirp:
	$(call _need_file,/usr/include/slirp/libvdeslirp.h,install_hosttool_pkgs)

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
# NB. This uses PYTHON.
# args: python-package action-name target
define _need_pylib =
    @$(SHELL_VERBOSE) \
    if ! echo -e "import sys\ntry:\n\timport $(1)\nexcept ImportError:\n\tsys.exit(1)" | $(PYTHON) > /dev/null; then \
        echo -e "\e[31mError: missing python '$(1)', $(2) it with \e[32m$(3)\e[0m"; \
        exit 1; \
    fi
endef

.PHONY: apt_update \
	boot \
	boot_all
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
	clone_all \
	clone_linux \
	clone_opensbi \
	connect \
	connect_all \
	download_all \
	download_prebuilt \
	download_rootfs \
	download_tt_installer \
	help \
	install_all \
	install_hosttool_pkgs \
	install_kernel_pkgs \
	install_qemu \
	install_tool_pkgs \
	install_tt_installer \
	_need_dtb \
	_need_dtc \
	_need_gcc \
	_need_git \
	_need_linux \
	_need_linux_tree \
	_need_luwen \
	_need_opensbi \
	_need_opensbi_tree \
	_need_python \
	_need_riscv64_toolchain \
	_need_rootfs \
	_need_tt_installer \
	_need_tmux \
	_need_unxz \
	_need_unzip \
	_need_libvdevslirp
