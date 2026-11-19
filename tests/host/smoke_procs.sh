#!/bin/sh
# Host smoke test for the portable target processes (no QNX needed).
set -u
B=$(dirname "$0")/../../build/host
pkill -f elv-plant 2>/dev/null; sleep 0.2
"$B/elv-plant" -r vf & P=$!
sleep 2
echo "-- hw boundary read by another process:"; "$B/elv-fault" hw-truth
"$B/elv-fault" transport on && echo "-- fault knob set: transport on"
echo "-- advisor attack (no elvrekd on host, so device opens fail with ENOENT):"
"$B/elv-advisor" -a | tail -3
kill $P; wait $P 2>/dev/null
echo "-- plant stopped"
