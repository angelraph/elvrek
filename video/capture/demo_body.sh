
# Demo capture for the video. Real stack, real output, on the QNX target.
# Appended to the helper functions of tests/fault_suite.sh.

line() { echo "$S" | tr ' ' '\n' | awk -F= '{a[$1]=$2} END {
  printf "tick=%s state=%s sw=%s depth_mm=%s force_n=%s etco2=%s charge=%s ready=%s shocks=%s ccf=%s compressions=%s machine=%s human=%s\n",
  a["tick"],a["state"],a["sw"],a["pos_mm"],a["force_n"],a["etco2"],a["charge"],a["ready"],a["shocks"],a["ccf"],a["compressions"],a["machine"],a["human"] }'; }

echo "@@scene boot"
echo "$ uname -a"; uname -a
echo "VM keep-awake: two idle threads at priority 1, below every Elvrek process"
up "-r vf -c" ""
echo "$ ls /dev/elvrek"; ls /dev/elvrek
echo "$ pidin (name, thread, priority)"
pidin -F "%N %b %p" | grep -E "elv" | sort -u
echo "$ elv-ctl status"; $B/elv-ctl status

echo "@@scene attack"
echo "$ elv-advisor -a     # the classifier tries every forbidden action"
$B/elv-advisor -a 2>&1

echo "@@scene shock"
echo "$ elv-ctl start"
$B/elv-ctl start
pressed=0; held=0
while :; do
    S=$($B/elv-ctl status)
    line
    v() { echo "$S" | tr ' ' '\n' | grep "^$1=" | cut -d= -f2; }
    if [ "$(v shocks)" = 0 ] && { [ "$(v charge)" = 1 ] || [ "$(v ready)" = 1 ]; }; then
        $B/elv-ctl shock
        [ $pressed = 0 ] && echo "crew: SHOCK pressed while charging (pre-authorised)"
        pressed=1
    fi
    if [ "$(v state)" = SHOCK_WINDOW ] && [ "$(v sw)" = 2 ] && [ "$(v shocks)" = 0 ]; then
        $B/elv-ctl shock; echo "crew: SHOCK pressed"
    fi
    [ "$(v state)" = ROSC_HOLD ] && held=$((held+1))
    [ $held -ge 40 ] && break
    [ "$(v tick)" -gt 190000 ] && break
    sleep 0.2
done

echo "@@scene watchdog"
up "-r asystole" ""
$B/elv-ctl start; sleep 4
S=$($B/elv-ctl status); line
echo "$ elv-fault hw-truth"; $B/elv-fault hw-truth
echo "$ slay elvrekd      # kill the safety process mid stroke"
slay -f elvrekd; sleep 2
echo "$ elv-fault hw-truth"; $B/elv-fault hw-truth
echo "$ cat /tmp/elv-super.state"; cat /tmp/elv-super.state; echo
down
echo "@@end"
