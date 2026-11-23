# Instant on: compressing before the rest of the system has booted

In a cardiac arrest every second without compressions costs blood flow to the brain. A mechanical CPR device that waits for its operating system to finish booting, mount its disks and bring up its network before it can push is wasting the first seconds it is on. QNX lets us avoid that: programs can live in the boot image itself and start from the very first boot script, before any disk exists.

## What we built

An instant-on image ([`qnx/instant-on/`](../qnx/instant-on/)). The hardware stand-in (`elv-plant`), the safety process (`elvrekd`) and `elv-ctl` are placed in the QNX boot image (IFS) and started at the top of the IFS boot script, before `startup.sh` starts the disk driver, mounts file systems, starts networking or the SSH server. Everything else boots around them while the piston is already working.

To measure it fairly we built a second image that starts the same three programs from the post startup script, at the end of the normal boot, the way an ordinary application starts. Both images are built from the same options and binaries, without a security policy, and run in emulator mode.

`elvrekd` records its milestones on `CLOCK_MONOTONIC`, which on QNX starts when the kernel starts: process started, control loop running, START taken, first full compression done. It only reads a clock in the loop, never prints. `elv-ctl boot`, started next to it, presses START as soon as the safety process answers, as a crew holding START at power on would, and prints the milestones once the first compression is done. On the host, [`qnx/instant-on/boot-time.sh`](../qnx/instant-on/boot-time.sh) launches QEMU, stamps every line from the serial port with the time since launch, and notes when SSH first answers.

## Results

QNX 8.0, QEMU with KVM, 2 virtual CPUs. Five boots of each image, alternating. Raw serial logs: [`docs/results/qnx-instant-on-ab.txt`](results/qnx-instant-on-ab.txt).

| Median of 5 boots | Instant on | Started after the normal boot |
|---|---|---|
| Control loop running and first push begins, kernel clock | **0.84 s** | 3.11 s |
| First full compression done, kernel clock | **1.39 s** (1.36 to 1.41) | 3.67 s (3.61 to 3.83) |
| First compression seen on the host, from QEMU launch | **1.71 s** (1.67 to 1.73) | 4.01 s (3.96 to 4.13) |
| Rest of the system booted ("Startup complete"), host | 3.4 to 4.0 s | 3.4 to 3.6 s |
| SSH answers, host | 3.6 to 4.5 s | 3.6 to 4.5 s |
| State after boot, all 10 boots | compressing, no fault, 0 rule violations | same |

Starting from the boot image moves the first compression **about 2.3 s earlier**, and the piston is already working while the disks are mounted and the network comes up. The rest of the boot is not measurably slower for it.

The gap from START to the first full compression, about 0.55 s, is one stroke at the default 110 per minute: the first push begins within a few milliseconds of the control loop running.

## What this does and does not show

* The times come from an emulator. "Power on" is the launch of QEMU, which includes QEMU's own start and its firmware loading the image. Real boards spend their own time in firmware before the kernel runs, so the kernel clock rows are the ones that carry over.
* The hardware is the physics model. A real device would also home the piston and check its sensors before the first push, which takes time we have not measured.
* With the programs started from the boot image, `elvrekd` starts before `slogger2`, so it runs without its system log; the status device and the boot milestones are unaffected. A product would start the logger first, which takes milliseconds.
* START is pressed by `elv-ctl boot` as soon as the device answers. A real crew positions the device first; what this shows is that the controller is never the reason they wait.

## Run it

```sh
make -C qnx                                   # target binaries
bash qnx/instant-on/build.sh                  # ~/elvrek-instant
bash qnx/instant-on/build-conventional.sh     # ~/elvrek-conventional
bash build/restart_guest.sh                   # stop any running guest first
bash qnx/instant-on/boot-time.sh 5            # five boots of the instant image
```
