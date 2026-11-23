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
# same keep-awake as the fault suite runs (SPIN=1): two idle threads at priority 1,
# below every Elvrek process, so the virtual CPUs are not parked between ticks
ssh $OPT "root@$IP" "slay -f elv-spin 2>/dev/null; sleep 2; rm -f /tmp/elv-spin; cp /data/elvrek/bin/elv-fault /tmp/elv-spin; nohup /tmp/elv-spin cpu-storm 2 3600 1 >/dev/null 2>&1 < /dev/null &"
# run detached on the target and fetch the log: a long ssh -tt session can stall its
# output on this guest even though the run itself completes
ssh $OPT "root@$IP" "rm -f /tmp/demo.log; EMU=1 nohup sh /data/elvrek/demo_capture.sh > /tmp/demo.log 2>&1 < /dev/null &"
for i in $(seq 1 300); do sleep 3; ssh $OPT "root@$IP" "grep -q @@end /tmp/demo.log" && break; done
ssh $OPT "root@$IP" "cat /tmp/demo.log" | stamp > "$OUT/demo.tsv"
ssh $OPT "root@$IP" "slay -f elv-spin" 2>/dev/null || true
echo "captured $(wc -l < "$OUT/demo.tsv") lines, boot $(wc -l < "$OUT/boot.tsv") lines"
