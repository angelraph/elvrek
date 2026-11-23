# One clock against two: the same arrest, run both ways

Today a crew using mechanical CPR works with two machines: the CPR device, and a separate AED or defibrillator. Each has its own clock. At every rhythm check the crew is the link between them: they hear the AED's prompt, pause the CPR device, wait for the analysis, wait for the charge, press SHOCK, and start the device again. Elvrek puts compressions, the rhythm check, the charger and the shock line under one controller and one clock. This page measures what that is worth on the same arrest.

## How it is measured

[`tests/host/workflow_ab.c`](../tests/host/workflow_ab.c), run with `make workflow-ab`. Three workflows, everything else identical:

| | Compressions | Rhythm decision | Charging | Crew steps |
|---|---|---|---|---|
| **Elvrek, one clock** | Elvrek's controller | read through the compressions; the 0.8 s look only when unsure | during compressions | press SHOCK when asked |
| **CPR device + AED** | the same controller with the defibrillator disconnected (`no_defib`) | the AED analyses on a still chest | after the analysis | pause the device, press SHOCK, restart the device |
| **CPR device + AED, crew pre-charges** | the same | the same | the crew starts charging during compressions, before the pause, which is the best a conventional crew can do | the same |

Shared by all three: the same physics model and ECG; the same rhythm algorithm and 0.8 s look (`elv_trusted_shockable`); the same charger (7 s to full); the same crew, 1 s to react to a prompt and 0.3 s to press SHOCK. The AED's 2-minute clock starts when compressions start and restarts when they resume, as AEDs do. AED analysis time differs widely between devices (Snyder and Morgan, Crit Care Med 2004), so it is swept: 3, 5 and 10 s.

Hands-off time is counted only while CPR is needed (until return of circulation). "Pre" is from the last compression to the shock, "post" from the shock to the next compression. Raw output: [`docs/results/host-workflow-ab.txt`](results/host-workflow-ab.txt).

## Results, with a 5 s AED analysis

| Arrest | Workflow | Shocks | First shock | Pre-shock pause | Post-shock pause | Hands-off in total | Compression fraction |
|---|---|---|---|---|---|---|---|
| VF, first shock converts | **Elvrek, one clock** | 1 | **120.7 s** | **0.30 s** | **0.00 s** | **0.3 s** | **99.8%** |
| | CPR device + AED | 1 | 133.3 s | 12.30 s | 1.01 s | 13.3 s | 91.7% |
| | CPR device + AED, crew pre-charges | 1 | 126.3 s | 5.30 s | 1.01 s | 6.3 s | 95.9% |
| VF that does not convert, 10 min | **Elvrek, one clock** | 4 | **120.7 s** | **0.30 s** | **0.00 s** | **1.2 s** | **99.8%** |
| | CPR device + AED | 4 | 133.3 s | 12.30 s | 1.01 s | 53.2 s | 91.1% |
| | CPR device + AED, crew pre-charges | 4 | 126.3 s | 5.30 s | 1.01 s | 25.2 s | 95.8% |
| Asystole, 10 min | **Elvrek, one clock** | 0 | | | | **1.6 s** | **99.7%** |
| | CPR device + AED (either) | 0 | | | | 24.0 s | 96.0% |
| Organised rhythm without a pulse, 10 min | **Elvrek, one clock** | 0 | | | | **0.0 s** | **100.0%** |
| | CPR device + AED (either) | 0 | | | | 24.0 s | 96.0% |

Depth stayed in 5 to 6 cm on 100% of strokes in every run, no rule was broken, and no shock was given with the cup off the chest's recoil point.

**Across the sweep** (3, 5, 10 s analysis), the conventional pre-shock pause is 10.3, 12.3 and 17.3 s, and 3.3, 5.3 and 10.3 s when the crew pre-charges. Elvrek's is 0.3 s in every case, and that 0.3 s is the crew's own press: the machine's part is a few milliseconds. Pre-charging is the strongest conventional practice and it removes the charge time, but it cannot remove the analysis on a still chest or the two hand-offs, because those come from having two clocks.

**Where the time goes.** With a 5 s analysis and the AED charging after it: parking the piston, 5 s of analysis, 7 s of charging and 0.3 s to press before the shock, then 1 s for the crew to restart the device. Elvrek charges during compressions, reads the rhythm through them, and restarts on its own the tick after the shock.

**One more difference the numbers show.** In the 10-minute VF run with a 3 s or 10 s analysis, the pre-charging arm gave 3 shocks, not 4: at one check the 0.8 s look on the still chest judged the VF not shockable. Elvrek uses the same look only when its reading through the compressions is unsure; here it was sure every time.

## What this does and does not show

* The conventional workflow is a model on our code, not a recording of a real AED or crew. The crew times (1 s, 0.3 s) are assumptions, the same for every arm; slower crews widen the gap, because Elvrek has fewer crew steps.
* Both arms use our physics model and synthetic ECG. On real arrest ECG the rhythm reading through compressions is good but not perfect (docs/ZERO-PAUSE.md): when it is unsure Elvrek takes the 0.8 s look, which this model includes.
* The `no_defib` switch exists only for this comparison: it disconnects the defibrillator from the controller and lets START resume a stopped device. It is off by default, `elvrekd` never sets it, and all QNX results are with it off.

## Run it

```sh
make workflow-ab                    # 3, 5 and 10 s analysis
./build/workflow_ab 4 6 8           # any other analysis times
```
