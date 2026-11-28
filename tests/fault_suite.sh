#!/bin/sh
# Elvrek: on-target fault suite (QNX 8.0). Run as root on the target:
#   sh /data/elvrek/fault_suite.sh          full suite (~9 min)
#   QUICK=1 sh /data/elvrek/fault_suite.sh  skips the 2-minute shock cycles
# Prints PASS/FAIL per case and exits non-zero if any case fails.
# Timing is reported in 1 ms control ticks, not microseconds.

B=${ELV_BIN:-/data/elvrek/bin}
FAILS=0
# EMU=1 runs the stack in its explicit emulator mode (wider sensor and
# watchdog timeouts for a virtual machine on a busy host). Results say so.
EF=""; [ -n "$EMU" ] && EF="-e"
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
    slay -f elv-super elv-advisor elv-advice-gate elv-telemetry elvrekd elv-plant 2>/dev/null
    sleep 1
}
# TYPES=1 starts every process under its own security policy type
T_PLANT=""; T_CTRL=""; T_SUPER=""; S_ADV=""; T_GATE=""
if [ -n "$TYPES" ]; then
    T_PLANT="on -T elv_plant_t"; T_CTRL="on -T elvrekd_t"; T_SUPER="on -T elv_super_t"; T_GATE="on -T elv_gate_t"
    S_ADV="-T elv_advisor_t"; ADV_TYPE="on -T elv_advisor_t"
fi
up() {   # $1 = plant args, $2 = advisor args
    down
    $T_PLANT $B/elv-plant $EF $1 &
    sleep 1
    $T_CTRL $B/elvrekd $EF &
    sleep 1
    $T_GATE $B/elv-advice-gate &
    $T_SUPER $B/elv-super $S_ADV -- $B/elv-advisor $2 &
    sleep 1
}
# Wait on the controller's own clock, not the wall clock: on a virtual
# machine the controller can lose time while paused. Gives up when the
# controller has counted $1 ticks, or after 15 real minutes.
wait_ticks_for() {  # $1 controller tick limit, $2 key, $3 value
    i=0
    while [ $i -lt 900 ]; do
        [ "$(st $2)" = "$3" ] && return 0
        [ "$(st tick)" -gt "$1" ] && return 1
        sleep 1; i=$((i+1))
    done
    return 1
}
# The crew behaves like a real one: it pre-authorises the shock while the
# charger fills, and presses again whenever the device asks for a press.
# A single press based on a status read a second old can land just as the
# window opens, where a press is ignored by design.
preauth_shock() {  # $1 controller tick limit
    lim=$(( $1 + 20000 )); i=0
    while [ $i -lt 900 ]; do
        S=$($B/elv-ctl status)
        v() { echo "$S" | tr ' ' '\n' | grep "^$1=" | cut -d= -f2; }
        [ "$(v shocks)" -ge 1 ] 2>/dev/null && return 0
        [ "$(v tick)" -gt "$lim" ] 2>/dev/null && return 1
        if [ "$(v charge)" = 1 ] || [ "$(v ready)" = 1 ] ||
           { [ "$(v state)" = SHOCK_WINDOW ] && [ "$(v sw)" = 2 ]; }; then
            $B/elv-ctl shock
        fi
        sleep 1; i=$((i+1))
    done
    return 1
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
[ -n "$SPIN" ] && echo "VM KEEP-AWAKE: two idle threads at priority 1 (below every Elvrek process)"
[ -n "$EMU" ] && echo "EMULATOR MODE: sensor timeout 50 ms, hardware watchdog 100 ms (hardware limits are 5 ms and 20 ms)"
echo

# T1 boot and devices
up "-r asystole" ""
for d in status advice operator piston shock; do
    [ -e /dev/elvrek/$d ] || bad "T1 device /dev/elvrek/$d" missing
done
t1=$(st tick); sleep 1; t2=$(st tick)
[ $((t2 - t1)) -ge 900 ] && ok "T1 control loop ticking" \
                          || bad "T1 control loop" "too few ticks in 1 s"

# T2 compressions: 100 to 120 a minute is 16.7 to 20 in 10 s, so at least 16
$B/elv-ctl start; sleep 10
c=$(st compressions); inv=$(st inv)
[ "$c" -ge 16 ] && [ "$inv" = 0 ] && ok "T2 compressions ($c in 10 s, 0 invariant violations)" \
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
# The claim: busy threads just below the control priority cannot stop the
# controller doing its job. Judged on what the chest gets: compressions on
# rate, every stroke 5 to 6 cm, no rule broken, no fault. Tick timing is
# not judged or printed: on a virtual machine it measures the host, and the
# timing of QNX itself is not published (licence section 4.6(i)).
c1=$(st compressions); i1=$(st inv); s1=$(truth strokes); r1=$(truth in_range)
$B/elv-fault cpu-storm 8 10 59 >/dev/null
c2=$(st compressions); i2=$(st inv); s2=$(truth strokes); r2=$(truth in_range)
base=0; storm=0; dc=$((c2 - c1)); ds=$((s2 - s1)); dr=$((r2 - r1))
[ $dc -ge 17 ] && [ "$i2" = "$i1" ] && [ $ds -ge 1 ] && [ $dr -eq $ds ] &&   [ "$(st state)" = COMPRESS ] && [ "$(st fault)" = NONE ] &&     ok "T5 CPU storm (8 threads, priority 59, 10 s): +$dc compressions, $dr of $ds strokes 5 to 6 cm, 0 violations" ||     bad "T5 CPU storm" "compressions +$dc, in range $dr of $ds, inv $i1 to $i2, state $(st state) fault $(st fault)"

# T6 stiff chest: force ceiling
$B/elv-fault stiff; sleep 10
f=$(truth max_force_n); fl=$(st force_limits)
le "$f" 650 && [ "$fl" -ge 1 ] && ok "T6 stiff chest: max force ${f} N, ceiling engaged $fl times" \
                               || bad "T6 stiff chest" "max force $f, limits $fl"

# T7 transport with IMU compensation
up "-r asystole -t" ""
$B/elv-ctl transport on; $B/elv-ctl start; sleep 60
s=$(truth strokes); r=$(truth in_range); $B/elv-fault hw-truth peaks | tail -1
pct=$(awk -v r="$r" -v s="$s" 'BEGIN{printf "%.1f", (s?100*r/s:0)}')
ge "$pct" 90 && ok "T7 transport: depth in 5-6 cm on $pct% of $s strokes" \
             || bad "T7 transport" "$pct% in range"

# T8 sensor dropout -> controlled retract
up "-r asystole" ""
$B/elv-ctl start; sleep 5
$B/elv-fault sensor-drop
wait_state state SAFE_HOLD 5 && sleep 1 && le "$(truth true_depth_mm)" 2 && \
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
preauth_shock 130000
wait_ticks_for 160000 state ROSC_HOLD
sh=$(st shocks); w=$(st max_window); so=$(truth shocks_off_recoil)
[ "$sh" = 1 ] && [ "$w" -lt 3000 ] && [ "$so" = 0 ] && \
    ok "T10 shock: 1 shock, hands-off window $w ticks (machine $(st max_machine)), 0 shocks off recoil, rhythm read through compressions: sure $(st zp_sure) unsure $(st zp_unsure)" || \
    bad "T10 shock" "shocks $sh window $w off_recoil $so fault $(st fault)"
wait_state state ROSC_HOLD 30 && ok "T10 trusted ROSC (EtCO2): parked, ventilating" \
                              || bad "T10 ROSC" "state $(st state) fault $(st fault) inv $(st inv)"
ccf=$(st ccf)
ge "$ccf" 0.90 && ok "T10 chest-compression fraction $ccf" || bad "T10 CCF" "$ccf"

# T12 rhythm read through compressions on a non-shockable rhythm. The
# 2-minute check is decided from the compressions: when the reading is sure
# there is no pause at all, when it is not the 0.8 s look runs as before.
# Either way nothing is shocked and the pause is never longer than the look.
# (On VFDB about half of asystole checks are sure; see docs/ZERO-PAUSE.md.)
up "-r asystole" ""
slay -f elv-super elv-advisor 2>/dev/null   # no advice: the controller's own reading decides
$B/elv-ctl start
wait_ticks_for 128000 zp_checks 1 || wait_ticks_for 128000 zp_unsure 1
sk=$(st zp_skipped); un=$(st zp_unsure); wn=$(st windows); sh=$(st shocks); mw=$(st max_window)
if [ "$sh" = 0 ] && [ $(( ${sk:-0} + ${un:-0} )) -ge 1 ] && [ "${mw:-0}" -le 1000 ]; then
    if [ "${sk:-0}" -ge 1 ]; then why="sure not shockable (score $(st zp_score)): no pause at all"
    else why="not sure (score $(st zp_score)): took the 0.8 s look, $mw ticks"; fi
    ok "T12 rhythm read through compressions: $why, no shock"
else
    bad "T12 rhythm read through compressions" "skipped $sk unsure $un windows $wn shocks $sh max_window $mw"
fi

# T11 malicious advisor
up "-r vf" "-m"
$B/elv-ctl start
# the advisor lies that the rhythm is organised, so it may talk the controller
# out of the first pre-charge; the second two minute boundary is always checked
preauth_shock 250000
sleep 2
ig=$(st adv_ignored); sh=$(st shocks); inv=$(st inv)
[ "$ig" -ge 100 ] && [ "$sh" -ge 1 ] && [ "$inv" = 0 ] && [ "$(st state)" = COMPRESS ] && \
    ok "T11 malicious advisor: $ig demands ignored, shock still delivered, 0 violations" || \
    bad "T11 malicious advisor" "ignored $ig shocks $sh windows $(st windows) retries $(st retries) inv $inv state $(st state) fault $(st fault)"
fi

down
echo
[ $FAILS -eq 0 ] && echo "PASS: all cases" || echo "FAIL: $FAILS case(s)"
exit $FAILS
