## Inspiration

When a heart stops, the people around it are fighting the clock. Every pause in chest compressions lowers the odds that the person walks out of hospital, and the pause right before a shock matters most. One study of 815 cardiac arrests found that every extra five seconds of that pause came with about 18% lower odds of survival.

Mechanical CPR machines already exist, and they are brilliant at one thing: they never get tired. But the compressions, the breaths and the defibrillator usually run on separate clocks, and a person in a moving ambulance has to stop, look, clear and press. I wanted to see what happens when one real-time brain owns all three.

## What it does

Elvrek is the safety controller for a chest compression robot. One QNX process wakes a thousand times a second and owns the piston, the timing of every breath, the defibrillator charger and the shock line.

* It keeps compressions at 100 to 120 a minute and 5 to 6 cm deep, with full recoil and a hard force ceiling.
* It charges the defibrillator while the piston keeps working, parks the arm at full recoil only for the shock itself, checks the rhythm on clean ECG, and resumes on the next tick. The machine's part of that pause is about 0.8 seconds, and the crew's time is counted separately so neither number hides the other.
* It reads the heart rhythm through its own compressions. Every defibrillator stops compressions to look at the heart, because compressions bury the ECG. Elvrek is the machine pushing, so it knows the phase of every stroke to the millisecond and subtracts its own artifact. When the reading is sure, the 2-minute check needs no pause at all, or the arm parks only for the shock pulse. When it is not sure, it takes the usual 0.8 s look. On QNX 8.0 the shock went through with 3 ticks of hands-off instead of 803.
* It times breaths so they never land on a downstroke.
* It holds depth on a moving ambulance by letting the motor driver correct for chest motion.
* A trusted sign of returning circulation, like a jump in carbon dioxide, parks the piston within one compression.
* The AI rhythm classifier can only suggest. An enforced QNX security policy gives it one door, a small advice gate, and the kernel refuses it the piston and the shock line. If it crashes or lies, compressions carry on.
* It starts pushing before the rest of the system has booted. The safety process lives in the QNX boot image and starts from the first boot script, so in every test boot the first full compression was done before any disk was mounted, while the same programs started at the end of a normal boot could only begin after it.
* The crew sees it all on a console drawn with QNX Screen: rate, depth and force, the countdown to the next rhythm check and what the reading through the compressions says, the charger, carbon dioxide and its trend. The console is read only and cannot touch the piston.
* If the safety process itself dies, the motor driver's own watchdog lifts the cup to full recoil. Nothing restarts the controller behind your back.

## How I built it

The core is plain C: a state machine, an independent monitor that checks six safety rules on every tick, and a physics model of the chest, the piston and the charger that sits behind the same interface a real motor driver will use.

On QNX 8.0 it runs as small processes with clear jobs. The safety process runs at SCHED_FIFO priority 60 on a timer pulse and serves its devices through resource managers. The classifier drops to an unprivileged user and locks away its abilities. A supervisor restarts the classifier but never the safety process, and System Launch and Monitor starts everything in order. Every process runs under its own security policy type, recorded with secpolgenerate and then tightened by hand. A custom boot image starts the controller before the disks and network. Everything is built with qcc and runs on QNX 8.0 in QEMU with KVM.

I wanted proof rather than claims, so each part has its own check:

* Kernel event traces of the control loop, read by a script in the repo, show the work per tick and how fast the controller gets the CPU back from a storm just below its priority.
* Thirty one requirements, each traced to the exact line of code, the host test and its check, and the QNX run where it passed. A script checks every link on every build and CI fails if the matrix goes stale.
* The same arrest is run three ways: Elvrek's one clock, a CPR device plus a separate AED, and the same with the crew charging early.

I checked the rhythm logic on real arrest ECG from PhysioNet. I tuned it on one database and scored it once on another it had never seen.

## Challenges I ran into

The first runs on QNX did not all pass, and that turned out to be the best part. Each failure led to a real fix rather than a looser test.

* One early run counted far fewer ticks than it should, and I first thought asking QNX for a 0.5 ms system tick fixed it. Later I tested that call on its own: on QNX 8.0 in QEMU the tick is a fixed 1 ms and the call is refused. The slow run was the virtual machine. Now the controller reads the tick instead of assuming it, and checks the return of every call that changes the system.
* A paused controller let a stiff chest reach 823 N. Now the motor driver has its own force limit, so the hard limit holds even when software is late.
* After a pause, the controller replayed missed ticks with the newest sample. The whole movement during the pause looked like it happened in one millisecond, the force guard tripped early and strokes came up short. Speed is now measured over real hardware sample time, and two host tests stall the controller on purpose so this cannot come back.
* At real hardware timings the virtual machine itself was the limit. QEMU runs inside WSL2, which is itself a virtual machine on Windows, and it can freeze for longer than 5 ms. I report those runs as they are.

## Accomplishments that I'm proud of

* 12 of 12 fault cases pass on QNX 8.0, two full runs in a row, with the rhythm read through the compressions. The earlier design passed 11 of 11 three runs in a row. They cover a malicious classifier, crashes, a CPU storm just below the control priority, a stiff chest, transport, sensor loss, killing the safety process, and a full shock with return of circulation.
* 7 of 7 forbidden actions by the classifier are refused by the operating system, and all 12 fault cases still pass with the security policy enforced. Even kept as root under its type, the classifier is refused every path to the piston and the shock line.
* Kernel traces on QNX, of our own code: median work per tick of 27 to 37 microseconds, and no tick in which our control work ran past 1 ms, quiet or under a CPU storm. Timing of QNX itself is not published, following the hackathon organisers' guidance on licence section 4.6(i), 28 Nov 2026.
* Instant on, five boots of each image: in every one the first compression was done before any disk was mounted; with a normal start, only after the whole system had booted.
* With the analysis taking 5 s, the hands-off time before a shock is 0.3 s with one clock, 12.3 s with a CPR device and a separate AED, and 5.3 s when that crew charges early.
* The crew console on QNX matched the host replay of the same log on 11 of 11 frames.
* 31 of 31 requirements traced to code, tests and QNX results, checked in CI.
* On real arrest ECG from PhysioNet, at 0 dB, reading through the compressions shocked 82.1% of VF at the check against 75.0% for always pausing, cut wrong shock advice for sinus from 2.3% to 0.7%, and needed no pause at all for 70% of sinus checks. The method and an honest disclosure (we had to look at the test database twice) are in docs/ZERO-PAUSE.md.
* 22 of 22 host scenarios pass with zero rule violations.
* Every run, including the ones that failed, is published in the repository.

## What I learned

Real time is not about being fast. It is about knowing exactly who is allowed to do what, and what happens when something goes quiet. QNX made that easy to express: priorities, separate processes, abilities that can be locked away, and a supervisor that knows which process must never come back on its own.

Owning the actuator turned out to be the biggest advantage. A defibrillator has to guess what the compressions did to the ECG; a robot that is pushing knows exactly. That one fact turned our weakest number into the feature I am proudest of.

I also learned to be honest about the hard parts. My rhythm check on real ECG finds 75% of VF windows and leaves 97.7% of normal rhythm alone. That is a big step up from where it started, but it is still short of clinical goals, so the website says so plainly.

## What's next for Elvrek

The next step is hardware: a Raspberry Pi 5 driving a linear actuator with a load cell, a recoil switch, a mechanical stop and a relay that lights a lamp instead of a shock, tested only on a training manikin. On real hardware I will run the same kernel traces to get a true worst case. After that, a better rhythm check, a handover summary for the hospital, and a conversation with paramedics about what they actually need in the back of an ambulance.

Elvrek is a research controller. It is not a medical device and must never be connected to a person.

## Where QNX is used

Everything below is in the repository, so you can check it yourself.

* `src/qnx/elvrekd.c` is the safety process. It reads the system tick with `ClockPeriod`, measures every missed tick with `ClockCycles`, runs at `SCHED_FIFO` priority 60, is woken by a `timer_create` pulse received with `MsgReceivePulse` on a private channel, serves `/dev/elvrek/status`, `advice`, `operator`, `piston` and `shock` through `resmgr_attach`, locks away abilities with `procmgr_ability`, and logs with `slog2`.
* `src/qnx/elv_advisor_main.c` is the classifier. It drops to an unprivileged user and locks its abilities. With `-a` it tries every forbidden action and counts the refusals.
* `src/core/elv_cancel.c` and `src/core/elv_rhythm.c` run inside the safety process: the artifact canceller on every ECG sample, and the rhythm features spread over the control ticks, one spectral bin per tick, so the 1 ms loop is never late because of them.
* `src/qnx/elv_super.c` restarts the classifier when it dies or hangs, and never restarts the safety process.
* `src/qnx/elv_plant_main.c` stands in for the hardware and holds the motor driver watchdog.
* `qnx/slm/elvrek-slm.xml` starts the stack with System Launch and Monitor and marks the safety process `repair none`.
* `qnx/secpol/elvrek.pol` is the enforced security policy, recorded with `secpolgenerate`, compiled with `secpolcompile` and booted with `--secpol=secure`. `src/qnx/elv_advice_gate.c` is the classifier's only door, and `elv_super` spawns the classifier straight into its type with `secpol_posix_spawnattr_settypeid`.
* `src/qnx/elv_console.c` is the crew console. It opens a Screen context and window and posts each frame with `screen_post_window`.
* `qnx/instant-on/` builds the instant on boot image: the controller is in the IFS and starts from the IFS boot script, and `elvrekd` stamps its milestones on `CLOCK_MONOTONIC`.
* `qnx/trace-run.sh` records kernel event traces with tracelogger, and `tools/tracecheck.py` reads the raw `.kev` files.
* `qnx/Makefile`, `qnx/build.sh` and `qnx/build-image.sh` build with qcc and make the image with mkqnximage.
* `tests/fault_suite.sh` runs on the target, and the raw output of every run is in `docs/results/`.

## Build and run

QNX SDP 8.0, target x86_64 in QEMU (KVM, 2 virtual CPUs), toolchain qcc (gcc_ntox86_64). Full instructions are in the README: https://github.com/angelraph/elvrek
