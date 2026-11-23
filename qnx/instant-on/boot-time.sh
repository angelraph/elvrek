#!/bin/bash
# Elvrek: time power on to first compression on the instant-on image.
#   bash qnx/instant-on/boot-time.sh [boots]       (stop the main guest first)
# "Power on" is the moment QEMU is launched on the host. Every serial line is
# stamped on the host clock from then. The guest also reports its own
# milestones on CLOCK_MONOTONIC, which starts when the QNX kernel starts.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMG=${ELV_INSTANT_DIR:-$HOME/elvrek-instant}
N=${1:-5}
IP=172.31.1.10; KEY=$HOME/.ssh/elvrek_qnx
OPT="-i $KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=1 -o BatchMode=yes"
OUT="$ROOT/build/instant"; mkdir -p "$OUT"
ACCEL="--enable-kvm --cpu host"; [ -w /dev/kvm ] || ACCEL=""
cd "$IMG"
for b in $(seq 1 $N); do
    LOG="$OUT/boot-$b.txt"
    T0=$(date +%s%N)
    ( qemu-system-x86_64 -smp 2 $ACCEL -m 1G \
        -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 \
        -netdev bridge,br=br0,id=net0 -device virtio-net-pci,netdev=net0,mac=52:54:00:99:43:62 \
        -kernel output/ifs.bin -serial stdio -monitor none -display none -no-reboot \
        -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
        -pidfile "$OUT/qemu.pid" < /dev/null 2>&1 | \
      python3 -u -c '
import sys, time
t0 = int(sys.argv[1]) / 1e9
for raw in sys.stdin.buffer:
    line = raw.decode("utf-8", "replace").rstrip()
    if line: print("%9.3f  %s" % (time.time() - t0, line), flush=True)
' "$T0" > "$LOG" ) &
    SSH=""
    for i in $(seq 1 600); do
        if [ -z "$SSH" ] && ssh $OPT root@$IP true < /dev/null 2>/dev/null; then
            SSH=$(( ($(date +%s%N) - T0) / 1000000 ))
        fi
        [ -n "$SSH" ] && grep -q "ELVREK BOOT" "$LOG" && break
        sleep 0.1
    done
    ST=$(ssh $OPT root@$IP "/proc/boot/elv-ctl status" < /dev/null 2>/dev/null)
    kill "$(cat "$OUT/qemu.pid")" 2>/dev/null; wait 2>/dev/null
    echo "boot $b: ssh_answered_host_ms=$SSH" >> "$LOG"
    echo "boot $b: status_after_ssh: $ST" >> "$LOG"
    echo "== boot $b"
    grep -E "ELVREK|Startup complete|Starting devb|Starting Networking|Starting sshd|elv-plant|elvrekd" "$LOG"
    echo "ssh answered at ${SSH} ms (host clock)"
    echo "$ST" | tr ' ' '\n' | grep -E "^(state|fault|compressions|missed|inv)=" | tr '\n' ' '; echo
    sleep 2
done
