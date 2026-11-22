#!/bin/bash
# Elvrek: build the QNX 8.0 QEMU image with a security policy mode.
#   bash qnx/image-policy.sh open                           record with secpolgenerate
#   bash qnx/image-policy.sh secure qnx/secpol/elvrek.pol   enforce the policy
# Boot it with qnx/suite-only.sh (no sudo needed once the br0 bridge exists).
set -e
source "$HOME/qnx800/qnxsdp-env.sh" > /dev/null
IMG=$HOME/elvrek-image; KEY=$HOME/.ssh/elvrek_qnx; IP=172.31.1.10
PP=""; [ -n "$2" ] && PP="--policy=$(readlink -f "$2")"
cd "$IMG"
ACCEL=no; [ -w /dev/kvm ] || ACCEL=yes
mkqnximage --type=qemu --arch=x86_64 --cpu=2 --ram=1G --tcg=$ACCEL \
           --ip="$IP" --ssh-ident="$KEY.pub" --noprompt --secpol="$1" $PP --build 2>&1 | tail -3
