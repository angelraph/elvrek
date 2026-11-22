"""Timing evidence from a QNX kernel trace (tracelogger + traceprinter).

Reads the text that traceprinter writes for a .kev file and measures, from
the kernel's own record, how Elvrek's 1 ms control thread really behaves:

  * pulse gap   time between two timer pulses making the thread READY
  * wake        READY -> RUNNING: how long the scheduler made it wait
  * work        RUNNING -> back to RECEIVE: the work of one tick, including
                any time it was preempted
  * inversions  moments where the control thread was READY and a thread of
                LOWER priority (for example a CPU storm thread) was running
                on a CPU at the same time; on a correct fixed priority
                scheduler this never lasts longer than a context switch

It also reports the same work figure for the hardware stand-in (priority 62,
the only Elvrek thread above the controller) and a fixed priority response
time bound for the controller: its own worst work plus the worst work of
every higher priority thread that can run on the same CPU, which must fit
inside the 1 ms period.

Usage: python -I tools/tracecheck.py trace.txt [more traces...]
"""
import re
import sys
from collections import defaultdict

EV = re.compile(r"^t:0x([0-9a-f]+) CPU:(\d+) THREAD\s+:(TH\w+)\s+pid:(\d+) tid:(\d+)")
NAME = re.compile(r"^\s+name:(\S+)")
PID = re.compile(r"^\s+pid:(\d+)")
CPS = re.compile(r"TRACE_CYCLES_PER_SEC:: (\d+)")


def pct(xs, q):
    if not xs:
        return 0.0
    s = sorted(xs)
    return s[min(len(s) - 1, int(q * (len(s) - 1)))]


def load(path):
    cps, names, events = 1.0, {}, []
    last_pid = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            m = CPS.search(ln)
            if m:
                cps = float(m.group(1)); continue
            m = EV.match(ln)
            if m:
                events.append((int(m.group(1), 16), int(m.group(2)), m.group(3), int(m.group(4)), int(m.group(5))))
                continue
            m = PID.match(ln)
            if m:
                last_pid = int(m.group(1)); continue
            m = NAME.match(ln)
            if m and last_pid is not None and m.group(1).startswith("./"):
                names[last_pid] = m.group(1)[2:]
    return cps, names, events


def analyse(path, storm_pid=None):
    cps, names, ev = load(path)
    us = 1e6 / cps
    by_name = {v: k for k, v in names.items()}
    ctl, plant = by_name.get("elvrekd"), by_name.get("elv-plant")
    storm = storm_pid or by_name.get("elv-fault")
    prio = {plant: 62, ctl: 60, storm: 59}
    gaps, wakes, works, pwork = [], [], [], []
    last_ready = None
    ready_at = run_at = None
    p_run = None
    running = {}            # cpu -> (pid, tid)
    inversion_events = 0
    storm_wakes = []        # wake latency when a storm thread held a CPU at READY
    storm_at_ready = False
    for t, cpu, st, pid, tid in ev:
        if st == "THRUNNING":
            running[cpu] = (pid, tid)
        if pid == ctl and tid == 1:
            if st == "THREADY":
                if last_ready is not None:
                    gaps.append((t - last_ready) * us)
                last_ready = ready_at = t
                storm_at_ready = storm is not None and any(p == storm for p, _ in running.values())
            elif st == "THRUNNING" and ready_at is not None:
                wakes.append((t - ready_at) * us)
                if storm_at_ready:
                    storm_wakes.append((t - ready_at) * us)
                run_at = t
                ready_at = None
            elif st == "THRECEIVE" and run_at is not None:
                works.append((t - run_at) * us)
                run_at = None
        if pid == plant and tid == 1:
            if st == "THRUNNING":
                p_run = t
            elif st in ("THRECEIVE", "THNANOSLEEP", "THSIGWAITINFO") and p_run is not None:
                pwork.append((t - p_run) * us)
                p_run = None
        # inversion: controller READY while a lower priority Elvrek or storm
        # thread holds a CPU
        if ready_at is not None and storm is not None:
            lower = [c for c, (p, _) in running.items() if p == storm]
            if lower and len(lower) == len(running):
                inversion_events += 1
    n = len(gaps)
    print("\n%s" % path)
    print("  control thread (elvrekd, priority 60): %d timer pulses in the trace" % (n + 1))
    print("  pulse gap   median %7.1f us   p99 %7.1f us   max %8.1f us" % (pct(gaps, .5), pct(gaps, .99), max(gaps or [0])))
    print("  wake        median %7.1f us   p99 %7.1f us   max %8.1f us" % (pct(wakes, .5), pct(wakes, .99), max(wakes or [0])))
    print("  work        median %7.1f us   p99 %7.1f us   max %8.1f us" % (pct(works, .5), pct(works, .99), max(works or [0])))
    if pwork:
        print("  hardware stand-in (elv-plant, priority 62) work: median %.1f us, max %.1f us" % (pct(pwork, .5), max(pwork)))
    over = sum(1 for w in works if w > 1000.0)
    print("  ticks whose work ran past 1 ms: %d of %d" % (over, len(works)))
    if storm is not None and storm_wakes:
        print("  pulses that arrived while a storm thread (priority 59) held a CPU: %d; the controller then ran after median %.1f us, p99 %.1f us, max %.1f us"
              % (len(storm_wakes), pct(storm_wakes, .5), pct(storm_wakes, .99), max(storm_wakes)))
    bound = (pct(works, 1.0) if works else 0) + (max(pwork) if pwork else 0)
    print("  response time bound: worst controller work %.1f us + worst higher priority work %.1f us = %.1f us (period 1000 us)"
          % (max(works or [0]), max(pwork or [0]), bound))
    return {"gaps": gaps, "wakes": wakes, "works": works, "bound": bound}


if __name__ == "__main__":
    args = sys.argv[1:]
    sp = None
    if "--storm" in args:
        i = args.index("--storm"); sp = int(args[i + 1]); del args[i:i + 2]
    for p in args:
        analyse(p, sp)
