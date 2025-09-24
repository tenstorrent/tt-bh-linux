#!/bin/bash
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0
# Create a simple initramfs with switch_root capability using riscv64 binaries
#
# Deps: debootstrap qemu-user-static cpio gzip

INITRAMFS_DIR=initramfs
OUTPUT_FILE=initramfs.cpio.gz
DEBIAN_ROOT=debian-riscv64-root
CACHE=$PWD/cache

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root"
    exit 99
fi

function info() {
    echo -e "\e[32m$1\e[0m";
}

function cleanup() {
    info "Cleaning up temporary files"
    rm -rf $INITRAMFS_DIR $DEBIAN_ROOT
}

set -e

# trap cleanup EXIT

info "Creating minimal riscv64 Debian system"
mkdir -p $CACHE
PACKAGES=util-linux,mount,coreutils,dash,stress-ng
debootstrap --cache-dir $CACHE --include $PACKAGES --arch riscv64 --variant=minbase trixie $DEBIAN_ROOT http://deb.debian.org/debian

info "Creating initramfs directory structure"
rm -rf $INITRAMFS_DIR
mkdir -p $INITRAMFS_DIR/{bin,sbin,etc,proc,sys,dev,tmp,mnt,newroot,lib,lib64}

info "Creating init script"
cat > $INITRAMFS_DIR/init << 'EOF'
#!/bin/sh
mount /dev/vda /newroot
cd /newroot/home/debian
rm -rf tmp*
stress-ng --abort --ignite-cpu --timestamp --verbose --timeout 0 --copy-file 2 --dentry 2
EOF
chmod +x $INITRAMFS_DIR/init

info "Copying riscv64 binaries from Debian system"
cp $DEBIAN_ROOT/bin/dash $INITRAMFS_DIR/bin/sh
cp $DEBIAN_ROOT/bin/mount $INITRAMFS_DIR/bin/
cp $DEBIAN_ROOT/bin/stress-ng $INITRAMFS_DIR/bin/
cp $DEBIAN_ROOT/bin/rm $INITRAMFS_DIR/bin/

info "Copying required riscv64 libraries"
for binary in $INITRAMFS_DIR/bin/* $INITRAMFS_DIR/sbin/*; do
    if [ -f "$binary" ] && [ -x "$binary" ]; then
        chroot $DEBIAN_ROOT ldd ${binary#$INITRAMFS_DIR} 2>/dev/null | awk '{print $3}' | while read lib; do
            if [ -f "$DEBIAN_ROOT$lib" ]; then
                mkdir -p "$INITRAMFS_DIR$(dirname "$lib")"
                cp "$DEBIAN_ROOT$lib" "$INITRAMFS_DIR$lib"
            fi
        done
        
        # Copy dynamic linker
        chroot $DEBIAN_ROOT ldd ${binary#$INITRAMFS_DIR} 2>/dev/null | grep 'ld-linux' | awk '{print $1}' | while read linker; do
            if [ -f "$DEBIAN_ROOT$linker" ]; then
                mkdir -p "$INITRAMFS_DIR$(dirname "$linker")"
                cp "$DEBIAN_ROOT$linker" "$INITRAMFS_DIR$linker"
            fi
        done
    fi
done

info "Creating device nodes"
cd $INITRAMFS_DIR/dev
mknod console c 5 1
mknod null c 1 3
mknod vda b 254 0
cd - >/dev/null

info "Generating cpio archive"
cd $INITRAMFS_DIR
find . | cpio -o -H newc | gzip > ../$OUTPUT_FILE
cd - >/dev/null

info "Created $OUTPUT_FILE ($(du -h $OUTPUT_FILE | cut -f1))"
