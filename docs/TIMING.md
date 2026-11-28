# Timing: what our own code does in each tick

The controller's own counters say it runs a 1 ms loop. This page checks the part that is ours, the work our code does in each tick, against an independent witness: a QNX kernel trace, taken with `tracelogger` on the target and read with `traceprinter`.

**What is not on this page.** On 28 Nov 2026 the hackathon organisers asked that entries do not publish performance evaluations of QNX software (licence section 4.6(i)), while the performance of an entrant's own software may be published. So timer pulse gaps, wake-up times, scheduling response and boot times, which measure QNX itself, are withheld here and in the raw result files. What remains is the work done by our own processes.

## How it was taken

* QNX 8.0, x86_64 in QEMU with KVM, 2 virtual CPUs, the same binaries as every other run. Kernel tracing needs the `trace` ability, which the enforced security policy rightly refuses even to root, so these traces were taken on the same image built without a policy.
* The full stack runs (hardware stand-in, safety process, advice gate, supervisor, classifier) and compresses. Two 5 s traces: one **quiet**, and one under a **CPU storm** of 8 busy threads at priority 59, just below the controller.
* Capture: [`qnx/trace-run.sh`](../qnx/trace-run.sh). Analysis: [`tools/tracecheck.py`](../tools/tracecheck.py). Summaries of our own work: [`docs/results/qnx-trace-quiet.txt`](results/qnx-trace-quiet.txt), [`qnx-trace-storm.txt`](results/qnx-trace-storm.txt).

## What the trace shows about our code

| Our processes | Quiet | Under the storm |
|---|---|---|
| Control code (elvrekd, priority 60), work per tick, median | **36.7 µs** | **26.9 µs** |
| Hardware stand-in (elv-plant, priority 62), work per tick, median | 36.4 µs | 28.4 µs |
| Ticks in which our control work ran past 1 ms | **0** | **0** |

At the median, one tick of our control work plus one tick of the hardware stand-in is well under a tenth of the 1 ms period. Compressions kept their rate in both traces and the stack stayed in COMPRESS with no fault; the fault suite's T5 checks the same thing on every run, judged on what the chest gets.

## Limits

These are measured inside a virtual machine, where a virtual CPU can be taken away in the middle of our work, so a single tick's work can look longer than it was. We do not claim a worst-case bound from this machine. That needs the same trace on real hardware, which is the next step (a Raspberry Pi 5). The controller is built for the gap meanwhile: it replays missed ticks with buffered motion samples, counts sensor silence only while awake, and its hardware watchdog acts on its own.
