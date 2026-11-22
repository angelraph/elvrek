# Elvrek: safety rules

Elvrek is a research controller. It is not a medical device and must never be connected to a person. This page describes how it is built to fail safe, so the design can be reviewed and tested.

## Who may do what

Only one process may touch the patient: `elvrekd`, the safety process. It owns the piston, the timing of breaths, the defibrillator charger and the shock line. The rhythm classifier is untrusted. It may send advice through `/dev/elvrek/advice` and nothing else, and QNX refuses it the piston, the shock line, the hardware memory, root and higher priority.

## The six rules

They live in [`src/core/elv_core.c`](src/core/elv_core.c) and are checked again on every tick by separate code in [`src/core/elv_inv.c`](src/core/elv_inv.c). On QNX, a broken rule forces a controlled retract.

| # | Rule | How it is enforced |
|---|---|---|
| 1 | The shock line closes only in the shock window, with the piston parked, the rhythm confirmed, the charger full and a press or pre-authorisation from the crew | The state machine has one place that closes the line; the monitor checks all five conditions on every tick |
| 2 | Force on the chest stays under 650 N | The controller stops pushing at 600 N and backs off; the motor driver has its own limit as well |
| 3 | The hands-off window closes within 3 s | A hard timeout resumes compressions, whatever the crew does |
| 4 | A trusted sign of returning circulation parks the piston within one compression | Pulse input or an abrupt rise in end-tidal CO2; the classifier's opinion is not enough |
| 5 | The classifier alone can never stop compressions, move the piston or close the shock line | Its advice can only skip one pre-charge; everything else is ignored and counted |
| 6 | Any fault lifts the piston to full recoil at a controlled speed and holds it there | Never stops at the bottom of a stroke |

## Layers of protection

1. **The controller** keeps every limit in its own logic.
2. **The independent monitor** checks the rules from the outside on every tick.
3. **The motor driver** has its own force limit and its own watchdog. If the safety process goes quiet for 20 ms, it lifts the cup and dumps the charger.
4. **The supervisor** never restarts the safety process. A controller that quietly comes back in the middle of a resuscitation is more dangerous than one that stays down and says so.
5. **QNX** fences the classifier with an enforced security policy: its only door is the advice gate, so a crash, a lie or even a root compromise of the AI cannot reach the piston or the shock line.

## What it will never do

* Shock outside the window, off recoil, or without the crew.
* Push past 650 N to reach a depth.
* Stop compressions because the classifier said so.
* Ignore a trusted sign of returning circulation.
* Stop with the cup pressed into the chest.

## Tested

Each rule has host scenarios (`make test`, 20 scenarios, 0 violations) and an on-target case in [`tests/fault_suite.sh`](tests/fault_suite.sh). See [RESULTS.md](RESULTS.md).
