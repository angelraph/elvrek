# Elvrek: hackathon submission

QNX Global Hackathon, "The power to shape the world". Deadline: **15 December 2026, 2:00pm ET**. Category: **Robotics and Medical**.

## Submission fields

| Field | Value |
|---|---|
| Project name | Elvrek |
| Elevator pitch | A real-time safety controller for mechanical CPR on QNX 8.0. One clock for compressions, breaths and the shock, so the pause before a shock stays short. |
| Category | Robotics and Medical |
| New or existing | New. Started on 19 November 2026, inside the submission period; the first commit is dated 19 November 2026 |
| Repository | https://github.com/angelraph/elvrek (public, Apache License 2.0) |
| Demo video | https://youtu.be/lBU1zjZX8Ok (2:41, under the three minute limit, public on YouTube) |
| Website | https://elvrek.vercel.app |
| QNX version | QNX SDP 8.0 |
| Target | QNX 8.0 for x86_64 in QEMU with KVM, 2 virtual CPUs, 1 GB. Also builds for aarch64le (Raspberry Pi 5) |
| Toolchain | qcc (gcc_ntox86_64), mkqnximage, System Launch and Monitor |
| Built with | C, QNX, QNX 8.0, QEMU, POSIX, Python, JavaScript, HTML, CSS, Vercel |
| Project story | [`docs/devpost-story.md`](docs/devpost-story.md) |
| Builder | [@angelraph](https://github.com/angelraph) |

## Build and run

```sh
source ~/qnx800/qnxsdp-env.sh
bash qnx/build.sh                 # every target program, built with qcc
bash qnx/run-on-qemu.sh           # the QNX 8.0 image, built with mkqnximage and booted in QEMU
EMU=1 bash qnx/suite-only.sh      # the 11 fault cases, run on the target
make test                         # the 20 host scenarios
```

## How it meets the QNX requirement

It runs on QNX 8.0 and most of what makes it safe comes from QNX: a 1 ms timer pulse on a private channel, `SCHED_FIFO` priorities, a 0.5 ms tick set with `ClockPeriod`, resource managers for every device the hardware is reached through, `procmgr_ability` and an unprivileged user to fence the AI classifier, a supervisor that never restarts the safety process, System Launch and Monitor, and `slog2`. File by file in [QNX.md](QNX.md).

## Evidence for reviewers

| Claim | Where to check |
|---|---|
| 11 of 11 fault cases pass on QNX 8.0, two runs in a row | [`docs/results/qnx-fault-suite-all-pass-3.txt`](docs/results/qnx-fault-suite-all-pass-3.txt), [`-4.txt`](docs/results/qnx-fault-suite-all-pass-4.txt) |
| 7 of 7 forbidden classifier actions refused by QNX | Case T3 in the files above, and 1:25 in the video |
| Shock on 803 ticks of machine time, then return of circulation | Case T10, and 1:36 in the video |
| Safety process killed, cup lifted to 0.0 mm, never restarted | Case T9, and 1:53 in the video |
| Runs at real hardware timings | [`qnx-fault-suite-strict-timing-1.txt`](docs/results/qnx-fault-suite-strict-timing-1.txt) (5 of 11), [`-2-spinner.txt`](docs/results/qnx-fault-suite-strict-timing-2-spinner.txt) (9 of 11) |
| Every failed run, and what it taught us | [RESULTS.md](RESULTS.md) |
| 20 of 20 host scenarios | `make test`, run on every push by GitHub Actions |

## AI tools

Claude Code, Anthropic's coding assistant, helped write and test the C code, the QNX build and test scripts, the website and the demo video. The voice-over in the video uses Microsoft Edge text to speech. Every result was run on QNX 8.0 and is published here, including the runs that failed.

## Not a medical device

Elvrek is a research controller tested against a physics model. It must never be connected to a person.
