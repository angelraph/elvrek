#!/bin/bash
# Elvrek: build the instant-on QNX 8.0 image for QEMU in ~/elvrek-instant.
# Same options as the main test image (no security policy), plus the snippets
# in this folder. Build the target binaries first: make -C qnx
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
source "$HOME/qnx800/qnxsdp-env.sh" > /dev/null
IMG=${ELV_INSTANT_DIR:-$HOME/elvrek-instant}; KEY=$HOME/.ssh/elvrek_qnx; IP=172.31.1.10
mkdir -p "$IMG/local/snippets"
cp -r "$HOME/elvrek-image/local/misc_files" "$IMG/local/" 2>/dev/null || true
B="$ROOT/build/qnx-x86_64"
sed "s#\${ELV_BIN}#$B#g" "$ROOT/qnx/instant-on/ifs_files.custom" > "$IMG/local/snippets/ifs_files.custom"
cp "$ROOT/qnx/instant-on/ifs_start.custom" "$IMG/local/snippets/ifs_start.custom"
cd "$IMG"
ACCEL=no; [ -w /dev/kvm ] || ACCEL=yes
mkqnximage --type=qemu --arch=x86_64 --cpu=2 --ram=1G --tcg=$ACCEL \
           --ip="$IP" --ssh-ident="$KEY.pub" --noprompt --secpol=no --build 2>&1 | tail -3
grep -n "elv" output/build/ifs.build
