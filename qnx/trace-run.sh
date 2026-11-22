#!/bin/sh
# Elvrek: capture two 5 s kernel traces on the target while the controller
# compresses, one quiet and one under a CPU storm (8 threads at priority 59).
# Run on the target as root (needs an image without an enforced policy, or a
# policy that grants the trace ability). Then copy /tmp/quiet.kev and
# /tmp/storm.kev to the host, convert with traceprinter and analyse with
# tools/tracecheck.py.
B=/data/elvrek/bin
cd $B
slay -f elv-super elv-advisor elv-advice-gate elvrekd elv-plant elv-fault 2>/dev/null; sleep 1
./elv-plant -e -r vf > /dev/null 2>&1 & sleep 1
./elvrekd -e > /dev/null 2>&1 & sleep 1
./elv-advice-gate > /dev/null 2>&1 & sleep 1
./elv-super -- ./elv-advisor > /dev/null 2>&1 & sleep 1
./elv-ctl start; sleep 4
pidin -F "%a %b %N %p" | grep -E "elv"
tracelogger -s 5 -f /tmp/quiet.kev > /tmp/tl1.out 2>&1
./elv-fault cpu-storm 8 9 59 > /dev/null 2>&1 &
sleep 1
tracelogger -s 5 -f /tmp/storm.kev > /tmp/tl2.out 2>&1
sleep 4
./elv-ctl status | tr " " "\n" | grep -E "^(tick|state|fault|missed|late|overrun|compressions)="
ls -la /tmp/quiet.kev /tmp/storm.kev
slay -f elv-super elv-advisor elv-advice-gate elvrekd elv-plant
echo TRACE-DONE
