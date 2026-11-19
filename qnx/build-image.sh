#!/bin/sh
# Elvrek: build and boot a QNX 8.0 x86_64 QEMU image with the stack on it.
# Run on the Linux/WSL host after:  source ~/qnx800/qnxsdp-env.sh
#
#   qnx/build-image.sh            build binaries + image, boot it in QEMU
#
# Uses mkqnximage (QNX SDP 8.0). mkqnximage options vary by SDP update; run
# `mkqnximage --help` once at the kill gate and adjust the two lines marked
# CHECK if the option names differ.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
WORK=${ELV_IMAGE_DIR:-$HOME/elvrek-image}

command -v qcc >/dev/null || { echo "qcc not found: source qnxsdp-env.sh first"; exit 1; }
make -C "$HERE" ARCH=x86_64

mkdir -p "$WORK/local/snippets" "$WORK/data/elvrek/bin"
cp "$ROOT"/build/qnx-x86_64/* "$WORK/data/elvrek/bin/"
cp "$ROOT/tests/fault_suite.sh" "$HERE/slm/elvrek-slm.xml" "$WORK/data/elvrek/"

# copy our files into the image's data partition                        CHECK
cat > "$WORK/local/snippets/data_files.custom" <<EOF
[uid=0 gid=0 perms=0755] /data/elvrek/bin=data/elvrek/bin
[uid=0 gid=0 perms=0755] /data/elvrek/fault_suite.sh=data/elvrek/fault_suite.sh
[uid=0 gid=0 perms=0644] /data/elvrek/elvrek-slm.xml=data/elvrek/elvrek-slm.xml
EOF

cd "$WORK"
mkqnximage --type=qemu --arch=x86_64 --clean --build                 # CHECK
echo
echo "Image built in $WORK. Boot it with:  cd $WORK && mkqnximage --run"
echo "Then on the target, as root:          sh /data/elvrek/fault_suite.sh"
