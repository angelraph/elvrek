#!/bin/bash
# Elvrek: the comparison image in ~/elvrek-conventional. Same binaries in the
# boot image, started from the post startup script instead of the top of the
# boot script.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
source "$HOME/qnx800/qnxsdp-env.sh" > /dev/null
IMG=$HOME/elvrek-conventional; KEY=$HOME/.ssh/elvrek_qnx; IP=172.31.1.10
mkdir -p "$IMG/local/snippets"
cp -r "$HOME/elvrek-image/local/misc_files" "$IMG/local/" 2>/dev/null || true
B="$ROOT/build/qnx-x86_64"
sed "s#\${ELV_BIN}#$B#g" "$ROOT/qnx/instant-on/ifs_files.custom" > "$IMG/local/snippets/ifs_files.custom"
: > "$IMG/local/snippets/ifs_start.custom"
cp "$ROOT/qnx/instant-on/post_start.conventional" "$IMG/local/snippets/post_start.custom"
cd "$IMG"
ACCEL=no; [ -w /dev/kvm ] || ACCEL=yes
mkqnximage --type=qemu --arch=x86_64 --cpu=2 --ram=1G --tcg=$ACCEL \
           --ip="$IP" --ssh-ident="$KEY.pub" --noprompt --secpol=no --build 2>&1 | tail -1
grep -n "ELVREK\|elv-" output/build/post_startup.sh | head
