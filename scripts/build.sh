#!/bin/bash

export DTS=$PWD/x280.dts
export DTB=$PWD/x280.dtb
export LINUX=$HOME/linux
export SBI=$HOME/opensbi

dtc $DTS > $DTB
if [[ $? -ne 0 ]] ; then
    echo "error: dtc failed to compile device tree source file"
    exit 1
fi

pushd $LINUX
if [ ! -f ".config" ]; then
  make CROSS_COMPILE=riscv64-linux-gnu- ARCH=riscv defconfig
fi
make CROSS_COMPILE=riscv64-linux-gnu- ARCH=riscv -j$(nproc)
popd

pushd $SBI
#CROSS_COMPILE=riscv64-linux-gnu- make FW_PIC=y FW_PAYLOAD_OFFSET=0x200000 PLATFORM=generic FW_PAYLOAD=y FW_PAYLOAD_PATH=$LINUX/arch/riscv/boot/Image FW_FDT_PATH=$DTB
CROSS_COMPILE=riscv64-linux-gnu- make FW_PIC=y FW_JUMP=y FW_JUMP_OFFSET=0x200000 FW_JUMP_FDT_OFFSET=0x100000  PLATFORM=generic
popd

