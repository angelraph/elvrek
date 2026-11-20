# Elvrek on QNX 8.0: integration spec

This page shows exactly how and where Elvrek uses QNX 8.0, so a reviewer can check every claim against the code. Line numbers refer to the files as they are in this repository.

## Target and toolchain

| | |
|---|---|
| QNX | QNX SDP 8.0 (QNX Everywhere licence) |
| Target | x86_64 in QEMU with KVM, 2 virtual CPUs, 1 GB. Image built with `mkqnximage` ([`qnx/build-image.sh`](qnx/build-image.sh), [`qnx/run-on-qemu.sh`](qnx/run-on-qemu.sh)) |
| Next target | aarch64le on a Raspberry Pi 5 (`bash qnx/build.sh aarch64le`) |
| Compiler | `qcc -Vgcc_ntox86_64` ([`qnx/Makefile`](qnx/Makefile), [`qnx/build.sh`](qnx/build.sh)) |
| Host | Ubuntu 24.04 in WSL2 on Windows |

## The safety process: `src/qnx/elvrekd.c`

| QNX API | Where | What it does for Elvrek |
|---|---|---|
| `ClockPeriod(CLOCK_REALTIME, NULL, &cp, 0)` | line 495 | Reads and logs the system tick the 1 ms timer runs on. On QNX 8.0 in QEMU it is a fixed 1 ms; asking for 0.5 ms returns ENOTSUP, so we read it rather than assume it |
| `pthread_setschedparam(..., SCHED_FIFO, ...)` | line 287 | The control thread runs at FIFO priority 60 |
| `ChannelCreate(_NTO_CHF_PRIVATE)` | line 290 | A private channel nobody else can connect to |
| `SIGEV_PULSE_INIT`, `timer_create(CLOCK_MONOTONIC, ...)` | lines 296 to 298 | A 1 ms timer that delivers a pulse at the control priority |
| `MsgReceivePulse` | line 320 | The heartbeat: the loop wakes once per pulse |
| `ClockCycles` | lines 323, 416 | Measures late wakeups, missed ticks and work overruns, and judges sensor silence on real time |
| `dispatch_create`, `resmgr_attach` | lines 205, 457, 476 | Serves `/dev/elvrek/status`, `operator`, `piston` and `shock` on one dispatch, and `/dev/elvrek/advice` on its own dispatch and channel so the security policy can give it its own type |
| `procmgr_ability` | line 272 | After start-up, denies and locks spawn, fork, setuid, setgid and other abilities it will never need again |
| `slog2_register`, `slog2f` | line 435 | Logs every state change, fault and rule violation |

If the loop wakes late, it replays the ticks it missed (at most 200) with buffered motion samples, so the state machine's clocks stay in step with real time. It never replays inside the shock window, so every tick there reaches the hardware.

## The classifier: `src/qnx/elv_advisor_main.c`

* `drop_privileges` (line 32) switches to uid 2002 and calls `procmgr_ability` (line 41) to lock its abilities.
* It maps the ECG ring read only, and has no way to the hardware memory.
* With `-a` it tries seven forbidden things: open `/dev/elvrek/shock`, `piston`, `operator` and `status` for writing, open the hardware shared memory read and write, raise its priority to 65, and `setuid(0)`. On the target all seven are refused, and the suite checks that ([`docs/results/`](docs/results/), case T3).

## The supervisor: `src/qnx/elv_super.c`

Restarts the classifier when it dies or stops sending advice for 3 s. If the safety process disappears it does **not** restart it: it records `safety_down=1` and leaves the piston to the hardware watchdog, because a controller that silently comes back mid resuscitation is worse than one that stays down and says so.

## The hardware stand-in: `src/qnx/elv_plant_main.c`

Runs at FIFO priority 62 and owns the hardware boundary in shared memory, exactly like the future motor driver. It holds the driver's own safeguards:

* a **hardware watchdog** (line 111): if the safety process stops publishing for 20 ms (100 ms in emulator mode), it retracts the piston to full recoil and dumps the charger;
* a **force limit** that will not push deeper above the limit, however late the controller is;
* **chest motion correction** in transport, added on every driver tick.

## System Launch and Monitor: `qnx/slm/elvrek-slm.xml`

Starts the plant, then the safety process, then the supervisor and the classifier, in order. The safety process is pinned to CPU 0 (`runmask 0x1`) and marked `repair none`; the supervisor runs on the other CPUs and is marked `restart`.

## Security policy: `qnx/secpol/`

A draft policy in the QNX 8.0 policy language gives each process its own type, and the advice channel its own type, so the classifier can be allowed to talk to `/dev/elvrek/advice` and nothing else. It is not compiled or enforced yet; today the fence is the unprivileged user and locked abilities, which is what T3 tests. [`qnx/secpol/README.md`](qnx/secpol/README.md) describes how it will be generated with `secpolgenerate` and tightened.

## Emulator mode

`elvrekd -e` and `elv-plant -e` widen the sensor timeout from 5 ms to 50 ms and the hardware watchdog from 20 ms to 100 ms, because a virtual machine on a laptop pauses for tens of milliseconds. Every status line and every result file says when it was used. Every other limit and rule is the same code that will run on a board. Runs at the real timings are published too ([RESULTS.md](RESULTS.md)).

## On-target tests: `tests/fault_suite.sh`

Eleven cases that run on the QNX target and print PASS or FAIL. They are judged on what the chest gets, timed on the controller's own clock, and the raw output of every run is in [`docs/results/`](docs/results/). Run them with `EMU=1 bash qnx/suite-only.sh` from the host.
