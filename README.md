<p align="center"><img src="web/assets/elvrek-logo.webp" alt="Elvrek" width="360"></p>

# Elvrek

**A safety controller for mechanical CPR, built on QNX 8.0.**

Elvrek is the brain of a chest compression robot. One real-time process on QNX owns the piston, the timing of each breath, the defibrillator charger and the shock line. Everything else, including the rhythm classifier, can only make suggestions, and the operating system stops it from doing more.

Please read this first: **Elvrek is a research controller. It is not a medical device and must never be connected to a person.** In this repository the piston and the chest are a physics model behind the same interface a real actuator driver will use. No shock is ever delivered; on a bench the shock line drives a lamp.

Website: see `web/`, deployed on Vercel.

## Why it exists

During a cardiac arrest the pause before a shock costs lives. In a study of 815 out-of-hospital arrests, every extra 5 seconds of pause before the shock was associated with about 18% lower odds of survival to discharge (Cheskes et al., Circulation, 2011). Guidelines ask for pauses under 10 seconds and for chest compressions at least 80% of the time.

Mechanical CPR devices already exist, but the compressions, the breaths and the defibrillator usually keep separate clocks and a person coordinates them. Elvrek puts them on one 1 ms clock:

* the charger fills while the piston keeps compressing;
* the piston parks at full recoil only for the shock itself;
* a trusted check confirms the rhythm on clean ECG, then the shock line closes;
* compressions resume on the next tick.

## What the controller guarantees

These rules live in `src/core/elv_core.c` and are checked independently on every tick by `src/core/elv_inv.c`. On QNX a broken rule forces a controlled retract.

1. The shock line closes only in the shock window, with the piston parked, the rhythm confirmed, the charger full and a press (or a pre-authorisation) from the crew.
2. Force on the chest stays under 650 N; the controller stops pushing at 600 N and backs off.
3. The hands-off window closes within 3 seconds, whatever the crew does.
4. A trusted sign of returning circulation (a pulse input or an abrupt rise in end-tidal CO2) parks the piston within one compression.
5. The classifier alone can never stop compressions, move the piston or close the shock line.
6. Any fault lifts the piston to full recoil at a controlled speed and holds it there. It never stops at the bottom of a stroke.

## How it is built on QNX

* `elvrekd` is the safety process. A timer pulse on a private channel wakes it every millisecond at SCHED_FIFO priority 60. It serves `/dev/elvrek/status`, `advice`, `operator`, `piston` and `shock` as resource manager devices, and locks away abilities it no longer needs after start-up.
* `elv-advisor` is the rhythm classifier. It drops to an unprivileged user, locks its abilities and sees only a read-only copy of the ECG. With `-a` it tries every forbidden action and counts the refusals.
* `elv-plant` stands in for the hardware and owns the hardware boundary in shared memory. It has its own watchdog: if the safety process goes quiet for 20 ms, the piston is retracted.
* `elv-super` restarts the advisor when it dies or hangs. It never restarts the safety process.
* `qnx/slm/elvrek-slm.xml` starts everything in order with System Launch and Monitor, pins the safety process to its own CPU and marks it `repair none`.
* `qnx/secpol/README.md` describes the security policy types and how the policy is generated and tightened.

## Results so far

On the QNX 8.0 target (QEMU with KVM, 2 virtual CPUs, built with qcc, `qnx/run-on-qemu.sh` then `EMU=1 bash qnx/suite-only.sh`), two complete runs of the 11 case fault suite, raw output in `docs/results/`:

* Passed on both runs: control loop at about 1,000 ticks a second, compressions on rate with 0 rule violations, 7 of 7 forbidden advisor actions refused by QNX, advisor killed and restarted while compressions continued, CPU storm below control priority with no extra missed ticks, stiff chest held to 613 N and 577 N (limit 650 N), sensor dropout to safe hold, safety process killed with the hardware watchdog lifting the cup to 0.0 mm and nothing restarting it, and a full shockable rhythm run with one shock, hands-off 1,338 and 1,311 ticks, return of circulation detected and CCF 99.1%.
* Moved with host load: T7 transport (100% on a quiet run, 53.9% and 34.7% on busy ones) and T11 malicious advisor (passed on run A; on run B all 338 forged demands were ignored, but host pauses cost 34 s and the shock came after the test stopped waiting).
* These runs use the explicit emulator mode (sensor timeout 50 ms, hardware watchdog 100 ms instead of 5 ms and 20 ms), because a virtual machine on a busy laptop pauses for tens of milliseconds.
* The target runs found and fixed real problems: a stretched 1 ms timer (now a 0.5 ms system tick), a supervisor bug, and a stiff chest pushed to 823 N while the controller was paused, which led to a force limit inside the driver itself.

On the host:

Host scenario suite (`make test`, the same core code that runs on QNX): 18 of 18 scenarios pass with no invariant violations. They cover a shockable rhythm with return of circulation, 30:2 mode, refractory VF, a dead, hung or malicious advisor, a true return of circulation the advisor denies, a stiff chest, sensor loss, an emergency stop mid stroke, asystole, PEA, a pre-authorised shock, a slow crew, a crew that never presses, and transport.

* Machine time in the shock window (park, confirm, discharge): 803 ticks of 1 ms.
* Human time is reported separately and is not part of the machine's number.
* Transport: depth stays in the 5 to 6 cm band on 100% of strokes with frame sensor compensation, 71% without it.
* A stiff chest that cannot reach 5 cm under the force ceiling is reported as a miss; depth is never bought with more force.

Rhythm checks on real arrest ECG (57 PhysioNet records). The trusted check measures five things on 0.8 s of clean ECG, including the share of power in the 3 to 9 Hz fibrillation band. `make rhythm-tune` tunes its thresholds on CUDB only, with a rule fixed in advance, and scores them once on VFDB. On VFDB, held out: VF found in 75.0% of windows, normal sinus left alone 97.7%, other rhythms 98.0%, asystole 90.0%. Before tuning it was 67.6%, 90.3%, 93.0% and 85.9%. **It still does not meet the AHA goals** (VF above 90%, sinus above 99%, asystole above 95%), so the 0.8 s step should be read as a timed pause with a careful check, not a diagnosis.

Timing is reported in control ticks. Microsecond figures from an emulator would measure the emulator, and the QNX Everywhere licence question about publishing performance results is still open.

## Build and run

Host tests (Linux or WSL with gcc):

    make test          # 18 safety scenarios
    make host-procs    # the portable target processes
    python3 -I tools/fetch_physionet.py && make ecg-eval

QNX target, QNX SDP 8.0, x86_64 in QEMU, toolchain qcc (gcc_ntox86_64):

    source ~/qnx800/qnxsdp-env.sh
    make -C qnx                  # or ARCH=aarch64le for a Raspberry Pi 5
    qnx/build-image.sh           # builds the image with mkqnximage
    sh /data/elvrek/fault_suite.sh   # on the target, as root

## Hardware, later

The safety code talks to `include/elv_hw.h` only. The next phase is a Raspberry Pi 5 driving a linear actuator with a load cell on the cup, a recoil limit switch, a mechanical stop behind the software limit and a relay that lights a lamp. Only a training manikin, never a person.

## Data and licence

Code: Apache License 2.0. ECG data comes from PhysioNet (CUDB and VFDB) under the Open Data Commons Attribution License; see `DATA-NOTICE.md`. The data is downloaded by the script, not stored in this repository.
