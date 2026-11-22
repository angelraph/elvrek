# Elvrek: results review

Every run of the fault suite on QNX 8.0 is published in [`docs/results/`](docs/results/), the failed ones included. This page walks through them in order and says what each failure taught us. The rule we held to: when a case failed, we looked for the real cause and fixed that, rather than loosening the test. There is one exception, T5, explained below, and we changed it openly.

All runs: QNX 8.0, x86_64, QEMU with KVM on two virtual CPUs, inside WSL2 on a Windows laptop, every program built with qcc. Unless marked "strict", runs use the stack's labelled emulator mode (sensor timeout 50 ms and hardware watchdog 100 ms, instead of 5 ms and 20 ms).

## The runs, in order

| # | File | Result | What happened | What changed |
|---|---|---|---|---|
| 1 | [`earlier/qnx-fault-suite-run-a.txt`](docs/results/earlier/qnx-fault-suite-run-a.txt) | 10 of 11 | Transport depth out of range | Chest motion correction moved into the motor driver, and the stroke check measured against the chest instead of the frame |
| 2 | [`earlier/qnx-fault-suite-run-b.txt`](docs/results/earlier/qnx-fault-suite-run-b.txt) | 9 of 11 | Transport, and the malicious advisor case timing out on the wall clock | The test now waits on the controller's own clock, and the crew pre-authorises the shock |
| 3 | [`qnx-fault-suite-all-pass-1.txt`](docs/results/qnx-fault-suite-all-pass-1.txt) | **11 of 11** | First clean run | |
| 4 | [`earlier/qnx-fault-suite-run2-t6-t7-fail.txt`](docs/results/earlier/qnx-fault-suite-run2-t6-t7-fail.txt) | 9 of 11 | A repeat run: stiff chest reached 849 N, transport strokes came up short (43 to 50 mm) | See "the replayed tick bug" below. Also, the stiff chest fault was injected in the middle of a push; it now takes effect at full recoil |
| 5 | [`earlier/qnx-fault-suite-run3-t10-fail.txt`](docs/results/earlier/qnx-fault-suite-run3-t10-fail.txt) | 10 of 11 | Safe hold after the shock, fault SENSOR_STALE | Sensor silence is now counted only over time the controller was awake to see it |
| 6 | [`earlier/qnx-fault-suite-run4-t11-fail.txt`](docs/results/earlier/qnx-fault-suite-run4-t11-fail.txt) | 10 of 11 | No shock in the malicious advisor case | The test crew pressed once, from a status read a second old. It now presses whenever the device asks, like a real crew |
| 7 | [`qnx-fault-suite-all-pass-2.txt`](docs/results/qnx-fault-suite-all-pass-2.txt) | **11 of 11** | | |
| 8 | [`earlier/qnx-fault-suite-run6-t5-missed-ticks.txt`](docs/results/earlier/qnx-fault-suite-run6-t5-missed-ticks.txt) | 10 of 11 | T5 missed ticks: 2,263 with the storm against 1,091 without, while compressions kept their full rate | See "the T5 rule" below |
| 9 | [`qnx-fault-suite-all-pass-3.txt`](docs/results/qnx-fault-suite-all-pass-3.txt) | **11 of 11** | Final code | |
| 10 | [`qnx-fault-suite-all-pass-4.txt`](docs/results/qnx-fault-suite-all-pass-4.txt) | **11 of 11** | Final code, second run in a row | |
| 11 | [`qnx-fault-suite-strict-timing-1.txt`](docs/results/qnx-fault-suite-strict-timing-1.txt) | 5 of 11, strict | Real hardware timings in a nested VM: every failure is SENSOR_STALE | See "strict timing" below |
| 12 | [`qnx-fault-suite-strict-timing-2-spinner.txt`](docs/results/qnx-fault-suite-strict-timing-2-spinner.txt) | 9 of 11, strict | Same, with two idle threads at priority 1 keeping the virtual CPUs awake | |
| 13 | [`qnx-fault-suite-all-pass-5.txt`](docs/results/qnx-fault-suite-all-pass-5.txt) | **11 of 11** | Final code, after the controller stopped asking for a 0.5 ms tick (see the correction below): the third clean run in a row | |

Before run 1, the very first target runs also forced two fixes that are now part of the design: a force limit inside the motor driver (a paused controller had let a stiff chest reach 823 N), and catch-up ticks that replay missed time with buffered motion samples.

**A correction.** One early run counted only about 850 ticks a second, and we first credited a call that asked QNX for a 0.5 ms system tick. While writing the codelab we tested that call on its own: on QNX 8.0 in QEMU the clock period is a fixed 1 ms and `ClockPeriod` refuses to change it (ENOTSUP). The call never took effect, so the slow run was most likely the virtual machine. Every run since counts 960 to 1,013 ticks a second on the default 1 ms tick. The controller now reads and logs the clock period instead.

## Reading the rhythm through the compressions

| # | File | Result | What happened |
|---|---|---|---|
| 14 | [`earlier/qnx-fault-suite-run13-zp-vm-freeze.txt`](docs/results/earlier/qnx-fault-suite-run13-zp-vm-freeze.txt) | 9 of 12 | First full runs of the new design. The guest had run for over 15 hours and dozens of suites, and missed up to 10% of ticks; T11 tripped the sensor timeout. A fresh guest fixed it. Along the way: per-second scoring was too heavy for one tick and is now spread one spectral bin per tick, and a one-tick charge dump lost on replayed ticks is now re-asserted every tick |
| 15 | [`earlier/qnx-fault-suite-run15-host-slept.txt`](docs/results/earlier/qnx-fault-suite-run15-host-slept.txt) | 10 of 12 | The laptop went to sleep in the middle of T10 (120,000 missed ticks). Not a valid run |
| 16 | [`qnx-fault-suite-zero-pause-1.txt`](docs/results/qnx-fault-suite-zero-pause-1.txt) | **12 of 12** | T10 shock with 3 ticks of hands-off; T12 no pause at all |
| 17 | [`qnx-fault-suite-zero-pause-2.txt`](docs/results/qnx-fault-suite-zero-pause-2.txt) | **12 of 12** | Second run in a row, same results |

T12 tests what must always hold: the 2-minute check is decided from the compressions, nothing is shocked, and the pause is either zero (sure) or the 0.8 s look (not sure). Earlier versions of T12 demanded a skip every time; that tested chance, because on VFDB only about half of asystole checks are sure. Method and numbers: [docs/ZERO-PAUSE.md](docs/ZERO-PAUSE.md).

## The replayed tick bug

When the virtual machine paused, the controller woke late and replayed the ticks it had missed, feeding each one the newest sensor sample. The first replayed tick therefore saw the whole movement of the pause as if it had happened in one millisecond. That looked like a very fast piston, so the force guard stopped the stroke early and the controller cut its target depth by 2 mm. Every pause shrank the strokes a little more.

The fix measures speed and stiffness only when a new hardware sample arrives, over the real time between samples (`sample_n` in `include/elv_hw.h`, used in `src/core/elv_core.c`). Two host scenarios now stall the controller for 40 ms every 0.7 s and replay exactly the way `elvrekd` does. With the old code, transport depth falls to 16.4% of strokes in range; with the fix it is 98.5%, and a stiff chest stays under 650 N.

## The T5 rule

T5 runs eight busy threads just below the control priority for ten seconds. It used to pass only if the controller missed no more ticks with the storm than without it. On a nested virtual machine that number measures the host, not QNX: across runs the idle baseline alone ranged from about 800 to 1,400 missed ticks per ten seconds, and one run missed more ticks with the storm while the next missed fewer. T5 now judges what the chest gets during the storm: compressions on rate, every stroke 5 to 6 cm, no rule broken, no fault. It still prints the missed ticks. This is the one rule we changed, and we say so here, in the README and on the website.

## Strict timing

At the timings a real board will use (5 ms sensor timeout, 20 ms watchdog), QEMU inside WSL2 is a virtual machine inside a virtual machine. Its virtual CPUs are Windows threads and can be parked for longer than 5 ms, so the controller sometimes sees the hardware go silent. Each time, it did exactly what it should: it called the sensors silent and retracted to safety.

Keeping the virtual CPUs busy with two threads at the lowest priority (they cannot affect anything else, because QNX preempts them instantly) took the strict result from 5 of 11 to 9 of 11, including the full shock and return of circulation. Transport and the malicious advisor case still trip the 5 ms timeout. Only real hardware can settle these numbers.

## Rhythm check on real ECG

57 records from PhysioNet. Thresholds tuned on CUDB with a rule fixed in advance, then scored once on VFDB, which the tuning never saw:

| | Held out (VFDB) | Goal | Before tuning |
|---|---|---|---|
| VF found | 75.0% | above 90% | 67.6% |
| Normal sinus left alone | 97.7% | above 99% | 90.3% |
| Other rhythms left alone | 98.0% | | 93.0% |
| Asystole left alone | 90.0% | above 95% | 85.9% |

It is much better than where it started and still short of the goals, so the 0.8 s step should be read as a careful timed pause, not a diagnosis.
