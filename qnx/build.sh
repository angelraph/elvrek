#!/bin/bash
# Elvrek: build every target binary with qcc. Run from the repository root:
#   bash qnx/build.sh            x86_64 (QEMU)
#   bash qnx/build.sh aarch64le  Raspberry Pi 5
set -e
ARCH=${1:-x86_64}
source "$HOME/qnx800/qnxsdp-env.sh" > /dev/null
echo "qcc: $(command -v qcc)"
make -B -C qnx ARCH="$ARCH" 2>&1 | tee "build/qcc-$ARCH.log" | grep -E "error|warning" || true
ls -la "build/qnx-$ARCH"
file "build/qnx-$ARCH/"* 2>/dev/null | head -10
