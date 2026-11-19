#!/bin/bash
# Elvrek: install QEMU in Ubuntu (or WSL) so mkqnximage can boot QNX 8.0 images.
# Asks for your sudo password once (typed into sudo, never seen by anyone else).
#
#   bash tools/setup-qemu.sh
set -e
sudo apt-get update
sudo apt-get install -y qemu-system-x86 qemu-utils bridge-utils net-tools iproute2 libxml2-utils
# allow hardware acceleration without root
sudo usermod -aG kvm "$USER"
sudo chmod 666 /dev/kvm || true
echo
qemu-system-x86_64 --version | head -1
ls -la /dev/kvm
echo "QEMU is ready."
