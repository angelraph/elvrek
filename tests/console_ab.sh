#!/bin/sh
# Elvrek: does the crew console disturb the 1 ms loop? Same guest, same
# rhythm, 60 s of compressions with and without the console drawing at 10 Hz
# (frames not saved). Prints missed and late ticks for each, twice, alternating.
B=${ELV_BIN:-/data/elvrek/bin}
SCR=${SCREEN_DIR:-/data/screen}
EF=""; [ -n "$EMU" ] && EF="-e"
st()   { $B/elv-ctl status "$1"; }
down() { slay -f elv-console elv-super elv-advisor elv-advice-gate elvrekd elv-plant 2>/dev/null; sleep 1; }
one() {  # $1 = with|without
    down
    $B/elv-plant $EF -r vf & sleep 1
    $B/elvrekd $EF & sleep 1
    [ "$1" = with ] && { LD_LIBRARY_PATH=$SCR:/proc/boot:/lib:/usr/lib $B/elv-console > /dev/null 2>&1 & sleep 1; }
    $B/elv-ctl start
    sleep 60
    echo "console $1: tick=$(st tick) missed=$(st missed) late=$(st late) overrun=$(st overrun) state=$(st state)"
}
for r in 1 2; do one without; one with; done
down
echo "@@done"
