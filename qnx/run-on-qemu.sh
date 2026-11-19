#!/bin/bash
# Elvrek: build a QNX 8.0 x86_64 QEMU image, boot it headless, copy the
# Elvrek binaries in over SSH and run the on-target fault suite.
# Run from the repository root on the Linux or WSL host:
#
#   bash qnx/run-on-qemu.sh          full suite (about 9 minutes)
#   QUICK=1 bash qnx/run-on-qemu.sh  skips the two minute shock cycles
#
# Output is saved to build/fault-suite.txt.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${ELV_IMAGE_DIR:-$HOME/elvrek-image}
KEY=$HOME/.ssh/elvrek_qnx
source "$HOME/qnx800/qnxsdp-env.sh" > /dev/null

[ -f "$KEY" ] || ssh-keygen -q -t ed25519 -N "" -f "$KEY"
bash "$ROOT/qnx/build.sh" x86_64 > /dev/null

mkdir -p "$IMG"
cd "$IMG"
ACCEL=no
[ -w /dev/kvm ] || ACCEL=yes           # fall back to software emulation
# A fixed address on the 172.31.1.0/24 bridge that mkqnximage sets up, so
# the host does not need a DHCP server (dnsmasq) for the guest.
IP=${ELV_TARGET_IP:-172.31.1.10}
mkqnximage --type=qemu --arch=x86_64 --cpu=2 --ram=1G --tcg=$ACCEL \
           --ip="$IP" --ssh-ident="$KEY.pub" --noprompt --build
mkqnximage --stop > /dev/null 2>&1 || true
# detached, so the guest keeps running after this script and its terminal end
setsid nohup mkqnximage --run=-h --assumed-ip="$IP" > /tmp/elvrek-qemu.log 2>&1 < /dev/null &
echo "Waiting for QNX at $IP..."
for i in $(seq 1 90); do
    ssh -i "$KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
        -o ConnectTimeout=2 -o BatchMode=yes "root@$IP" true 2>/dev/null && break
    sleep 2
done
echo "QNX target is up at $IP"

SSH="ssh -i $KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR root@$IP"
SCP="scp -i $KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
$SSH "mkdir -p /data/elvrek/bin"
$SCP "$ROOT"/build/qnx-x86_64/* "root@$IP:/data/elvrek/bin/"
$SCP "$ROOT/tests/fault_suite.sh" "root@$IP:/data/elvrek/"
$SSH "uname -a; pidin info | head -3"
$SSH "QUICK=$QUICK sh /data/elvrek/fault_suite.sh" 2>&1 | tee "$ROOT/build/fault-suite.txt"
echo "Saved to build/fault-suite.txt"
