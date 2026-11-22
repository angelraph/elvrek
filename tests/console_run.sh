#!/bin/sh
# Elvrek: crew console run on the QNX target, for the docs and the video.
# Needs QNX Screen running (see docs/CONSOLE.md). Run as root on the target:
#   sh /data/elvrek/console_run.sh
# Writes /data/con/log.txt (every status line the console drew) and a full
# Screen frame every 30 s into /data/con. Two runs back to back:
#   A  VF arrest: charge during compressions, pre-authorised shock, return of
#      circulation, piston parked
#   B  sinus rhythm, no advisor: the 2-minute check read through compressions
B=${ELV_BIN:-/data/elvrek/bin}
SCR=${SCREEN_DIR:-/data/screen}
OUT=${CON_OUT:-/data/con}
EF=""; [ -n "$EMU" ] && EF="-e"

st()   { $B/elv-ctl status "$1"; }
down() { slay -f elv-super elv-advisor elv-advice-gate elvrekd elv-plant 2>/dev/null; sleep 1; }
up() {
    down
    $B/elv-plant $EF $1 &
    sleep 1
    $B/elvrekd $EF &
    sleep 1
    $B/elv-advice-gate &
    sleep 1
    [ -n "$2" ] && { $B/elv-super -- $B/elv-advisor &  sleep 1; }
}

[ -e /dev/screen ] || { echo "QNX Screen is not running"; exit 1; }
rm -rf $OUT; mkdir -p $OUT
slay -f elv-console 2>/dev/null
LD_LIBRARY_PATH=$SCR:/proc/boot:/lib:/usr/lib $B/elv-console -l $OUT/log.txt -o $OUT -e 300 > $OUT/console.out 2>&1 &
sleep 2

echo "run A: VF, shock, return of circulation"
up "-r vf -c" adv
$B/elv-ctl start
held=0; i=0
while [ $i -lt 600 ]; do
    S=$($B/elv-ctl status)
    v() { echo "$S" | tr ' ' '\n' | grep "^$1=" | cut -d= -f2; }
    if [ "$(v shocks)" = 0 ] && { [ "$(v charge)" = 1 ] || [ "$(v ready)" = 1 ] ||
         { [ "$(v state)" = SHOCK_WINDOW ] && [ "$(v sw)" = 2 ]; }; }; then
        $B/elv-ctl shock
    fi
    [ "$(v state)" = ROSC_HOLD ] && held=$((held+1))
    [ $held -ge 20 ] && break
    sleep 0.5; i=$((i+1))
done
echo "A: $($B/elv-ctl status)"

echo "run B: sinus rhythm, rhythm read through compressions"
up "-r sinus" ""
$B/elv-ctl start
i=0
while [ $i -lt 400 ]; do
    [ "$(st zp_checks)" -ge 1 ] 2>/dev/null && break
    [ "$(st zp_unsure)" -ge 1 ] 2>/dev/null && break
    sleep 0.5; i=$((i+1))
done
sleep 10
echo "B: $($B/elv-ctl status)"
down
sleep 2
slay -s SIGINT elv-console 2>/dev/null; sleep 2
cat $OUT/console.out
ls $OUT | wc -l
echo "@@done"
