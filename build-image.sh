#!/bin/bash
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0
# Create an ext4 rootfs with Debian debootstrap
# Runs as root, and requires binfmt-misc to be set up so the host can run
# riscv64 binaries

# qemu-user-static is better as it allows us to set the path to use 
# libc6-dev-riscv64-cross without having to set up multiarch. However,
# multiarch is more flexible because then you can install any package from the
# archive as a build dependency.
#
# Deps: 
# qemu-user-static qemu-utils debootstrap

DISK_IMAGE=debian-riscv64.qcow2
SIZE=1G
USER=debian
HOSTNAME=tt-blackhole

MOUNT=/mnt
CACHE=$PWD/cache

if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit 99
fi

function info() {
    echo -e "\e[32m$1\e[0m";
}

function cleanup() {
    info "Unmounting $MOUNT and shutting down qemu-nbd"
    umount -ql $MOUNT/dev $MOUNT/proc $MOUNT/sys $MOUNT
    sync; sync; sync
    killall qemu-nbd

    if [ -e $DISK_IMAGE ]; then
        info "Converting qcow2 to raw disk image"
        qemu-img convert $DISK_IMAGE ${DISK_IMAGE%.qcow2}.img
    fi
}

set -e

info "Creating $DISK_IMAGE ($SIZE) and mounting on /dev/nbd0"
qemu-img create -f qcow2 $DISK_IMAGE $SIZE
modprobe nbd
qemu-nbd -c /dev/nbd0 $DISK_IMAGE
# NBD needs a moment to get going
sleep 4
mkfs -t ext4 -L root /dev/nbd0
mount /dev/nbd0 $MOUNT

trap cleanup EXIT

info "Installing system with debootstrap"
mkdir -p $CACHE
PACKAGES=sudo,openssh-server,systemd-timesyncd,vim,python3,make,gcc,libc6-dev,neowofetch
debootstrap --cache-dir $CACHE --include $PACKAGES --arch riscv64 trixie $MOUNT http://deb.debian.org/debian

info "Mounting filesystems"
mount -o bind,ro /dev $MOUNT/dev
mount -t proc none $MOUNT/proc
mount -t sysfs none $MOUNT/sys

info "Adding 'debian' user"
chroot $MOUNT adduser --disabled-password --comment '' $USER
usermod -R $MOUNT -a -G sudo $USER 
# Unlock the account
passwd -R $MOUNT -d $USER 

info "Setting up system with '$HOSTNAME' as hostname"
echo $HOSTNAME > $MOUNT/etc/hostname
echo "127.0.0.1     $HOSTNAME" >> $MOUNT/etc/hosts

info "Configiring all network devices to use dhcp"
cat - >>$MOUNT/etc/systemd/network/dhcp.network <<EOF
[Match]
Name=e*
Type=ether

[Network]
DHCP=ipv4
EOF
systemctl --root $MOUNT enable systemd-networkd.service

info "Configuring systemd for headless, firmwareless system"
# Instead of graphical
systemctl --root $MOUNT set-default multi-user.target

# Quieter
mkdir -p $MOUNT/etc/systemd/system.conf.d/
echo ShowStatus=auto >> $MOUNT/etc/systemd/system.conf.d/status.conf

MASKED_SERVICES="getty-static.service cron.service e2scrub_reap.service systemd-pstore.service systemd-tpm2-setup-early.service systemd-pcrmachine.service systemd-hibernate-clear.service kmod-static-nodes.service systemd-pcrextend.socket systemd-pcrlock.socket timers.target systemd-pcrphase.service systemd-random-seed.service systemd-tpm2-setup.service systemd-networkd-persistent-storage.service sys-fs-fuse-connections.mount systemd-hwdb-update.service"
for service in $MASKED_SERVICES; do
    systemctl --root $MOUNT mask $service
done

info "Enabling ssh empty password auth"
mkdir -p $MOUNT/etc/ssh/sshd_config.d/
echo "PermitEmptyPasswords yes" >> $MOUNT/etc/ssh/sshd_config.d/permit-empty-passwords.conf

# TODO: Detect if not running in CI and copy the user's keys in
# info "Copying SSH keys from $HOME/.ssh/*.pub"
# mkdir -m700 $MOUNT/home/debian/.ssh/
# cat ~/.ssh/*.pub > $MOUNT/home/debian/.ssh/authorized_keys
# chown -R 1000 --reference $MOUNT/home/debian $MOUNT/home/debian/.ssh
