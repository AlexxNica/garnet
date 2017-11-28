#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -e

# List of packages to install into the system on top of the minimal base
# image.
EXTRA_PACKAGES=(
   # Make ourselves bootable
   linux-image-amd64
   grub-efi

   # Misc utilities
   ca-certificates
   cpuid
   curl
   debootstrap
   git
   less
   sudo
   tmux
   unzip
   vim
   wget

   # Allows mounting the root fs as RO with a ramdisk overlay for ephemeral
   # writes.
   bilibop-lockfs

   # Support zircon build deps:
   texinfo
   libglib2.0-dev
   autoconf
   libtool
   libsdl1.2-dev
   build-essential

   # Unixbench deps:
   libx11-dev
   libgl1-mesa-dev
   libxext-dev
   make
   perl
   perl-modules

   # hdparm benchmark
   hdparm

   # QEMU/KVM guest support.
   qemu-kvm
)

usage() {
    echo "usage: ${0} [mountpoint]"
    echo ""
    echo "  Bootstraps a debian system image into the partition mounted at"
    echo "  [mountpoint]. The built system will contain the minimal base Debian"
    echo "  system along with whats needed for the following benchmarks:"
    echo ""
    echo "       * hdparm"
    echo "       * unixbench"
    echo ""
    echo "  Additional packages can be manually added by chrooting into the"
    echo "  created system and running apt directly. To use this partition"
    echo "  in a guest system simply pass the block device file to guest."
    echo ""
    echo "  This script creates a user with username/password of bench:password"
    echo "  with sudo access."
    echo ""
    echo "Example:"
    echo ""
    echo "  $ sudo mount /dev/sda1 /mnt"
    echo "  $ sudo bootstrap-debian.sh /mnt"
    echo ""
    echo "Add additional packages:"
    echo "  $ sudo chroot /mnt /bin/bash"
    echo "  # apt install <package>"
    echo ""
    exit 1
}

# We expect a single positional argument specifying the mount point.
check_args() {
    if [ "$#" -ne 1 ]; then
        usage
    fi
}

check_deps() {
  type -P debootstrap &>/dev/null && return 0

  echo "Required package debootstrap is not installed. (sudo apt install debootstrap)"
  exit 1
}

check_mountpoint() {
  mountpoint -q $1 && return 0

  echo "Provided path '$1' is not a mountpoint. Check your arguments."
  echo ""
  usage
}

# Stage2 is run from within the chroot of the new system so all system commands
# modify the new system and not the host.
bootstrap_stage2() {
    # Install additional packages.
    DEBIAN_FRONTEND=noninteractive apt-get -y --allow-unauthenticated install ${EXTRA_PACKAGES[*]}

    # Setup grub.
    grub-install --target x86_64-efi --efi-directory /boot --removable
    cat >> /etc/grub.d/40_custom << EOF
menuentry "Zircon" {
  insmod chain
  echo "Loading gigaboot..."
  chainloader /EFI/BOOT/gigaboot.efi
}
EOF
    update-grub

    # Create default account.
    local username="bench"
    local default_password="password"
    useradd ${username} -G sudo
    echo "${username}:${default_password}" | chpasswd
    echo "Default login/password is ${username}:${default_password}" > /etc/issue

    # Setup home directory.
    local user_home=/home/${username}
    mkdir -p ${user_home}

    # Set login shell.
    chsh -s /bin/bash ${username}

    # Get unix-bench
    pushd ${user_home}
    local unixbench_zip="${user_home}/unixbench.zip"
    wget https://github.com/kdlucas/byte-unixbench/archive/master.zip -O "${unixbench_zip}"
    unzip "${unixbench_zip}"
    rm "${unixbench_zip}"
    popd

    # Setup hostname.
    echo "zircon-guest" > /etc/hostname

    # Make sure all created files have appropriate ownership.
    chown -R ${username}:${username} ${user_home}

    # Clear out the package cache.
    apt clean
}

if [ "${SECOND_STAGE}" != "true" ]; then
    check_args "$@"

    check_deps

    check_mountpoint "${1}"

    debootstrap testing "${1}" http://deb.debian.org/debian/

    # Copy ourselves into the chroot and run the second stage.
    cp "${BASH_SOURCE[0]}" "${1}/second-stage.sh"
    mount --bind /dev "${1}/dev"
    SECOND_STAGE=true chroot ${1} "/second-stage.sh"
    umount "${1}/dev"
else
    bootstrap_stage2

    rm "/second-stage.sh"
fi
