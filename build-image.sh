#!/bin/bash
# Create an ext4 rootfs with Debian

# qemu-user-static is better as it allows us to set the path to use 
# libc6-dev-riscv64-cross without having to set up multiarch. However,
# multiarch is more flexible because then you can install any package from the
# archive as a build dependency.
#
# Deps: 
# qemu-user-static qemu-utils debootstrap

FILE=debian-riscv64.qcow2
SIZE=1G
USER=debian
HOSTNAME=tt-blackhole

MOUNT=/mnt
CACHE=$PWD/cache

qemu-img create -f qcow2 $FILE $SIZE
modprobe nbd
qemu-nbd -c /dev/nbd0 $FILE
# NBD needs a moment to get going
sleep 4
mkfs -t ext4 -L root /dev/nbd0
mount /dev/nbd0 $MOUNT

mkdir -p $CACHE
PACKAGES=sudo,openssh-server,systemd-timesyncd,vim,python3,make,gcc,libc6-dev,neowofetch
debootstrap --cache-dir $CACHE --include $PACKAGES --arch riscv64 trixie $MOUNT http://deb.debian.org/debian

mount -o bind,ro /dev $MOUNT/dev
mount -t proc none $MOUNT/proc
mount -t sysfs none $MOUNT/sys

# Add 'debian' user
chroot $MOUNT adduser --disabled-password --comment '' $USER
usermod -R $MOUNT -a -G sudo $USER 
# Unlock the account
passwd -R $MOUNT -d $USER 

echo $HOSTNAME > $MOUNT/etc/hostname
echo "127.0.0.1     $HOSTNAME" >> $MOUNT/etc/hosts

cat - >>$MOUNT/etc/systemd/network/dhcp.network <<EOF
[Match]
Name=e*
Type=ether

[Network]
DHCP=ipv4
EOF
systemctl --root $MOUNT enable systemd-networkd.service

# Instead of graphical
systemctl --root $MOUNT set-default multi-user.target

# Quieter
mkdir $MOUNT/mnt/etc/systemd/system.conf.d/
echo ShowStatus=auto >> $MOUNT/etc/systemd/system.conf.d/status.conf

MASKED_SERVICES="getty-static.service cron.service e2scrub_reap.service systemd-pstore.service systemd-tpm2-setup-early.service systemd-pcrmachine.service systemd-hibernate-clear.service kmod-static-nodes.service systemd-pcrextend.socket systemd-pcrlock.socket timers.target systemd-pcrphase.service systemd-random-seed.service systemd-tpm2-setup.service systemd-networkd-persistent-storage.service sys-fs-fuse-connections.mount systemd-hwdb-update.service"
for service in $MASKED_SERVICES; do
    systemctl --root $MOUNT mask $service
done

# TODO: print instructions on how to add ssh key

umount -l $MOUNT/dev $MOUNT/proc $MOUNT/sys $MOUNT
sync; sync; sync
killall qemu-nbd

qemu-img convert $FILE ${FILE%.qcow2}.img
