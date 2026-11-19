#!/bin/bash
# Elvrek: quick health check on the running QNX target. Copies the latest
# binaries, starts the stack on a shockable rhythm and prints the safety
# process's own counters every 10 s for a minute.
#   bash qnx/probe.sh
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IP=${ELV_TARGET_IP:-172.31.1.10}
OPT="-i $HOME/.ssh/elvrek_qnx -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
scp $OPT "$ROOT"/build/qnx-x86_64/* "root@$IP:/data/elvrek/bin/" > /dev/null
ssh $OPT "root@$IP" '
B=/data/elvrek/bin
slay -f elv-super elv-advisor elv-telemetry elvrekd elv-plant 2>/dev/null; sleep 1
$B/elv-plant -r vf -c & sleep 1
$B/elvrekd & sleep 1
$B/elv-super -- $B/elv-advisor & sleep 1
$B/elv-ctl start
for i in 1 2 3 4 5 6; do
  sleep 10
  $B/elv-ctl status | tr " " "\n" | grep -E "^(tick|state|fault|missed|late|overrun|inv|compressions|adv_age|adv_msgs)=" | tr "\n" " "; echo
done
pidin -F "%a %b %N %p %J" | grep -E "elv|procnto" | head -20
slay -f elv-super elv-advisor elvrekd elv-plant 2>/dev/null
'
