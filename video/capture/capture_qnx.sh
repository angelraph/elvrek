#!/bin/bash
# Reboots the QNX guest, records its serial boot log with timestamps, then
# runs the demo capture on the target with every line timestamped.
# Run in WSL from the repository root: bash video/capture/capture_qnx.sh
set -e
ROOT=$(pwd)
OUT=$ROOT/video/capture
IMG=$HOME/elvrek-image
IP=172.31.1.10
OPT="-i $HOME/.ssh/elvrek_qnx -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=3 -o BatchMode=yes"
stamp() { while IFS= read -r l; do printf '%s\t%s\n' "$(date +%s.%N)" "${l%$'\r'}"; done; }

# stop the running guest by its pid, then boot fresh and record the console
if [ -f "$IMG/output/qemu.pid" ]; then kill "$(cat "$IMG/output/qemu.pid")" 2>/dev/null || true; sleep 3; fi
cd "$IMG"; : > output/qemu.out
date +%s.%N > "$OUT/boot_start.txt"
qemu-system-x86_64 -smp 2 --enable-kvm --cpu host -m 1G \
    -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 \
    -netdev bridge,br=br0,id=net0 -device virtio-net-pci,netdev=net0,mac=52:54:00:99:43:62 \
    -kernel output/ifs.bin -serial file:output/qemu.out -display none \
    -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
    -pidfile output/qemu.pid -daemonize
(timeout 60 tail -F output/qemu.out 2>/dev/null | stamp > "$OUT/boot.tsv") &
for i in $(seq 1 60); do ssh $OPT "root@$IP" true 2>/dev/null && break; sleep 1; done
sleep 2; kill %1 2>/dev/null || true
cd "$ROOT"

# copy binaries and the demo script (helpers from the fault suite + body)
sed -n '1,/^echo "Elvrek fault suite on/p' tests/fault_suite.sh | sed '$d' > build/demo_capture.sh
cat video/capture/demo_body.sh >> build/demo_capture.sh
sed -i 's/\r$//' build/demo_capture.sh
ssh $OPT "root@$IP" "mkdir -p /data/elvrek/bin"
scp $OPT build/qnx-x86_64/* "root@$IP:/data/elvrek/bin/" > /dev/null
scp $OPT build/demo_capture.sh "root@$IP:/data/elvrek/" > /dev/null
ssh -tt $OPT "root@$IP" "EMU=1 sh /data/elvrek/demo_capture.sh" 2>&1 | stamp > "$OUT/demo.tsv"
echo "captured $(wc -l < "$OUT/demo.tsv") lines, boot $(wc -l < "$OUT/boot.tsv") lines"
