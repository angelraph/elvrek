# Timing, from the kernel's own record

The controller's own counters say it runs a 1 ms loop. This page checks that against an independent witness: a QNX kernel trace, taken with `tracelogger` on the target and read with `traceprinter`, which records every time a thread becomes ready, starts running and blocks again.

## How it was taken

* QNX 8.0, x86_64 in QEMU with KVM, 2 virtual CPUs, the same binaries as every other run. Kernel tracing needs the `trace` ability, which the enforced security policy rightly refuses even to root, so these traces were taken on the same image built without a policy. The policy only acts when a process connects to something, not on the 1 ms path.
* The full stack runs (hardware stand-in, safety process, advice gate, supervisor, classifier) and compresses. Two 5 s traces: one **quiet**, and one under a **CPU storm** of 8 busy threads at priority 59, just below the controller.
* Capture: [`qnx/trace-run.sh`](../qnx/trace-run.sh). Analysis: [`tools/tracecheck.py`](../tools/tracecheck.py). The raw traces are in [`docs/results/`](results/) (`qnx-trace-quiet.kev`, `qnx-trace-storm.kev`) with the analysis output next to them, so anyone can rerun it.

## What the trace shows

| Control thread (elvrekd, priority 60) | Quiet | Under the storm |
|---|---|---|
| Timer pulses recorded | 4,273 | 4,589 |
| Gap between pulses, median | 1.055 ms | 1.008 ms |
| Work per tick, median | **36.7 µs** | **26.9 µs** |
| Work per tick, 99th percentile | 209 µs | 161 µs |
| Ticks whose work ran past 1 ms | **0 of 4,090** | **0 of 4,417** |
| Hardware stand-in (priority 62) work per tick, median | 36.4 µs | 28.4 µs |

**Priorities hold.** In the storm trace, every time a pulse arrived while a priority 59 storm thread held a CPU (11 times in the record), the controller was running again after a median of 29.6 µs and at most 41.5 µs. The storm never kept it waiting. Compressions kept their rate in both traces and the stack stayed in COMPRESS with no fault.

**Budget.** At the median, one tick of control work plus one tick of the only higher priority Elvrek thread is about 55 to 75 µs, under 8% of the 1 ms period.

## What a virtual machine cannot prove

The tails in these traces belong to the virtual machine, not to QNX:

* the gap between pulses reaches 10.8 ms and the 99th percentile is about 2.7 ms, because the host (Windows, then WSL2, then QEMU) sometimes parks a virtual CPU;
* the worst "work" figures (up to 0.9 ms) are ticks during which the virtual CPU was taken away mid-work: the trace clock keeps running while the guest is not.

A fixed priority response time bound built from those worst values (controller work plus the hardware stand-in's work) comes to 1.4 to 1.7 ms, over the period. We do not claim a worst-case bound from this machine. That needs the same trace on real hardware, which is the next step (a Raspberry Pi 5). The controller is built for the gap meanwhile: it replays missed ticks with buffered motion samples, counts sensor silence only while awake, and its hardware watchdog acts on its own.

Timing is reported in microseconds here only as measured by the kernel trace on an emulator, with the limits above; elsewhere the project reports control ticks and pass or fail, while the QNX Everywhere licence question about publishing performance results is open.
