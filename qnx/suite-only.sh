#!/bin/bash
# Elvrek: run the fault suite on the QNX image built by qnx/run-on-qemu.sh.
# If the guest is not running, boots it directly with QEMU (no sudo needed
# once the br0 bridge exists), then copies the latest binaries and script.
#
#   bash qnx/suite-only.sh            full suite
#   QUICK=1 bash qnx/suite-only.sh    skips the two minute shock cycles
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${ELV_IMAGE_DIR:-$HOME/elvrek-image}
IP=${ELV_TARGET_IP:-172.31.1.10}
KEY=$HOME/.ssh/elvrek_qnx
OPT="-i $KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=3 -o BatchMode=yes"

if ! ssh $OPT "root@$IP" true 2>/dev/null; then
    echo "Booting the QNX image in QEMU (KVM, 2 CPUs, 1 GB)..."
    cd "$IMG"
    ACCEL="--enable-kvm --cpu host"
    [ -w /dev/kvm ] || ACCEL=""
    qemu-system-x86_64 -smp 2 $ACCEL -m 1G \
        -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 \
        -netdev bridge,br=br0,id=net0 -device virtio-net-pci,netdev=net0,mac=52:54:00:99:43:62 \
        -kernel output/ifs.bin -serial file:output/qemu.out -display none \
        -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
        -pidfile output/qemu.pid -daemonize
    for i in $(seq 1 60); do ssh $OPT "root@$IP" true 2>/dev/null && break; sleep 2; done
    cd "$ROOT"
fi
echo "QNX target at $IP: $(ssh $OPT "root@$IP" 'uname -a')"

sed -i 's/\r$//' "$ROOT/tests/fault_suite.sh"
ssh $OPT "root@$IP" "slay -f elv-super elv-advisor elv-telemetry elvrekd elv-plant 2>/dev/null; sleep 1; mkdir -p /data/elvrek/bin"
scp $OPT "$ROOT"/build/qnx-x86_64/* "root@$IP:/data/elvrek/bin/" > /dev/null
scp $OPT "$ROOT/tests/fault_suite.sh" "root@$IP:/data/elvrek/" > /dev/null
ssh $OPT "root@$IP" "QUICK=$QUICK EMU=$EMU TYPES=$TYPES sh /data/elvrek/fault_suite.sh" 2>&1 | tee "$ROOT/build/fault-suite${EMU:+-emulator}.txt"
