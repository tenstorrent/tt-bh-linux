#!/bin/bash

export BASE_DIR=$HOME

export LINUX_DIR=$BASE_DIR/linux
export LINUX_REPO=https://github.com/tenstorrent/linux
export LINUX_BRANCH=dfustini/wip/tt-bh-linux

export SBI_DIR=$BASE_DIR/opensbi
export SBI_REPO=https://github.com/tenstorrent/opensbi.git
export SBI_BRANCH=dfustini/wip/tt-bh-linux

export CROSS_COMPILE=riscv64-linux-gnu-
export ARCH=riscv

if [ ! -d "$LINUX_DIR" ]; then
  git clone $LINUX_REPO
fi

pushd $LINUX_DIR
git checkout $LINUX_BRANCH
if [ ! -f ".config" ]; then
  make defconfig
  ./scripts/config --enable ARCH_TENSTORRENT
  ./scripts/config --enable NONPORTABLE
  ./scripts/config --enable HVC_RISCV_SBI
  make olddefconfig
fi
make -j$(nproc)
popd

if [ ! -d "$SBI_DIR" ]; then
  git clone https://github.com/tenstorrent/opensbi.git
fi

pushd $SBI_DIR
git checkout $SBI_BRANCH
CROSS_COMPILE=riscv64-linux-gnu- make FW_PIC=y FW_JUMP=y FW_JUMP_OFFSET=0x200000 FW_JUMP_FDT_OFFSET=0x100000  PLATFORM=generic
