#!/bin/sh
# Elvrek: on-target fault suite (QNX 8.0). Run as root on the target:
#   sh /data/elvrek/fault_suite.sh          full suite (~9 min)
#   QUICK=1 sh /data/elvrek/fault_suite.sh  skips the 2-minute shock cycles
# Prints PASS/FAIL per case and exits non-zero if any case fails.
# Timing is reported in 1 ms control ticks, not microseconds.

B=${ELV_BIN:-/data/elvrek/bin}
FAILS=0
ADV_TYPE=${ADV_TYPE:-}          # e.g. "on -T elv_advisor_t" when secpol is enforcing

st()   { $B/elv-ctl status "$1"; }
truth(){ $B/elv-fault hw-truth | tr ' ' '\n' | grep "^$1=" | cut -d= -f2; }
sup()  { tr ' ' '\n' < /tmp/elv-super.state | grep "^$1=" | cut -d= -f2; }
ok()   { echo "PASS $1"; }
bad()  { echo "FAIL $1: $2"; FAILS=$((FAILS+1)); }
# integer compare on values that may be floats
ge()   { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a>=b)}'; }
le()   { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a<=b)}'; }

down() {
    slay -f elv-super elv-advisor elv-telemetry elvrekd elv-plant 2>/dev/null
    sleep 1
}
up() {   # $1 = plant args, $2 = advisor args
    down
    $B/elv-plant $1 &
    sleep 1
    $B/elvrekd &
    sleep 1
    $B/elv-super -- $B/elv-advisor $2 &
    sleep 1
}
wait_state() {  # $1 key, $2 value, $3 timeout s
    i=0
    while [ $i -lt $3 ]; do
        [ "$(st $1)" = "$2" ] && return 0
        sleep 1; i=$((i+1))
    done
    return 1
}

echo "Elvrek fault suite on $(uname -a)"
echo "physics plant, not a patient; timing in control ticks"
echo

# T1 boot and devices
up "-r asystole" ""
for d in status advice operator piston shock; do
    [ -e /dev/elvrek/$d ] || bad "T1 device /dev/elvrek/$d" missing
done
t1=$(st tick); sleep 1; t2=$(st tick)
[ $((t2 - t1)) -ge 900 ] && ok "T1 control loop ticking ($((t2 - t1)) ticks in 1 s)" \
                          || bad "T1 control loop" "only $((t2 - t1)) ticks in 1 s"

# T2 compressions
$B/elv-ctl start; sleep 10
c=$(st compressions); inv=$(st inv)
[ "$c" -ge 17 ] && [ "$inv" = 0 ] && ok "T2 compressions ($c in 10 s, 0 invariant violations)" \
                                  || bad "T2 compressions" "c=$c inv=$inv"

# T3 advisor attack: every forbidden path refused by the OS
if $ADV_TYPE $B/elv-advisor -a > /tmp/elv-attack.txt 2>&1; then
    ok "T3 advisor attack: $(tail -1 /tmp/elv-attack.txt)"
else
    bad "T3 advisor attack" "$(tail -1 /tmp/elv-attack.txt)"
fi

# T4 advisor killed mid-compression
c1=$(st compressions); slay -f elv-advisor; sleep 6; c2=$(st compressions)
r=$(sup restarts)
[ $((c2 - c1)) -ge 10 ] && [ "${r:-0}" -ge 1 ] && \
    ok "T4 advisor killed: compressions continued (+$((c2 - c1))), restarted ($r)" || \
    bad "T4 advisor killed" "dc=$((c2 - c1)) restarts=$r"

# T5 CPU storm below the control priority
m1=$(st missed); c1=$(st compressions)
$B/elv-fault cpu-storm 8 10 59 >/dev/null
m2=$(st missed); c2=$(st compressions)
[ "$m1" = "$m2" ] && [ $((c2 - c1)) -ge 17 ] && \
    ok "T5 CPU storm (8 threads, priority 59, 10 s): 0 missed ticks, +$((c2 - c1)) compressions" || \
    bad "T5 CPU storm" "missed $m1 -> $m2, compressions +$((c2 - c1))"

# T6 stiff chest: force ceiling
$B/elv-fault stiff; sleep 10
f=$(truth max_force_n); fl=$(st force_limits)
le "$f" 650 && [ "$fl" -ge 1 ] && ok "T6 stiff chest: max force ${f} N, ceiling engaged $fl times" \
                               || bad "T6 stiff chest" "max force $f, limits $fl"

# T7 transport with IMU compensation
up "-r asystole -t" ""
$B/elv-ctl transport on; $B/elv-ctl start; sleep 60
s=$(truth strokes); r=$(truth in_range)
pct=$(awk -v r="$r" -v s="$s" 'BEGIN{printf "%.1f", (s?100*r/s:0)}')
ge "$pct" 90 && ok "T7 transport: depth in 5-6 cm on $pct% of $s strokes" \
             || bad "T7 transport" "$pct% in range"

# T8 sensor dropout -> controlled retract
$B/elv-fault sensor-drop; sleep 2
[ "$(st state)" = SAFE_HOLD ] && le "$(truth true_depth_mm)" 2 && \
    ok "T8 sensor dropout: SAFE_HOLD, cup at recoil" || \
    bad "T8 sensor dropout" "state $(st state), depth $(truth true_depth_mm)"

# T9 safety process killed -> hardware watchdog
up "-r asystole" ""
$B/elv-ctl start; sleep 5
slay -f elvrekd; sleep 2
w=$(truth hw_watchdog_trips); d=$(truth true_depth_mm)
[ "$w" -ge 1 ] && le "$d" 2 && [ "$(sup safety_down)" = 1 ] && \
    ok "T9 safety process killed: HW watchdog retracted (depth $d mm), not restarted" || \
    bad "T9 safety process killed" "trips $w depth $d safety_down $(sup safety_down)"

if [ -z "$QUICK" ]; then
# T10 VF: charge while compressing, shock window, ROSC
up "-r vf -c" ""
$B/elv-ctl start
wait_state sw 2 140 && $B/elv-ctl shock
sleep 30
sh=$(st shocks); w=$(st max_window); so=$(truth shocks_off_recoil)
[ "$sh" = 1 ] && [ "$w" -lt 3000 ] && [ "$so" = 0 ] && \
    ok "T10 shock: 1 shock, hands-off window $w ticks, 0 shocks off recoil" || \
    bad "T10 shock" "shocks $sh window $w off_recoil $so"
wait_state state ROSC_HOLD 30 && ok "T10 trusted ROSC (EtCO2): parked, ventilating" \
                              || bad "T10 ROSC" "state $(st state)"
ccf=$(st ccf)
ge "$ccf" 0.90 && ok "T10 chest-compression fraction $ccf" || bad "T10 CCF" "$ccf"

# T11 malicious advisor
up "-r vf" "-m"
$B/elv-ctl start
wait_state sw 2 260 && $B/elv-ctl shock
sleep 2
ig=$(st adv_ignored); sh=$(st shocks); inv=$(st inv)
[ "$ig" -ge 100 ] && [ "$sh" -ge 1 ] && [ "$inv" = 0 ] && [ "$(st state)" = COMPRESS ] && \
    ok "T11 malicious advisor: $ig demands ignored, shock still delivered, 0 violations" || \
    bad "T11 malicious advisor" "ignored $ig shocks $sh inv $inv state $(st state)"
fi

down
echo
[ $FAILS -eq 0 ] && echo "PASS: all cases" || echo "FAIL: $FAILS case(s)"
exit $FAILS
