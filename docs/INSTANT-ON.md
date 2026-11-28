# Instant on: compressing before the rest of the system has booted

In a cardiac arrest every second without compressions costs blood flow to the brain. A mechanical CPR device that waits for its operating system to finish booting, mount its disks and bring up its network before it can push is wasting the first seconds it is on. QNX lets us avoid that: programs can live in the boot image itself and start from the very first boot script, before any disk exists.

## What we built

An instant-on image ([`qnx/instant-on/`](../qnx/instant-on/)). The hardware stand-in (`elv-plant`), the safety process (`elvrekd`) and `elv-ctl` are placed in the QNX boot image (IFS) and started at the top of the IFS boot script, before `startup.sh` starts the disk driver, mounts file systems, starts networking or the SSH server. Everything else boots around them while the piston is already working.

For comparison we built a second image that starts the same three programs from the post startup script, at the end of the normal boot, the way an ordinary application starts. Both images are built from the same options and binaries, without a security policy, and run in emulator mode.

`elv-ctl boot`, started next to the safety process, presses START as soon as the safety process answers, as a crew holding START at power on would, and prints a line once the first full compression is done. [`qnx/instant-on/boot-time.sh`](../qnx/instant-on/boot-time.sh) records the serial console of each boot.

## Results

QNX 8.0, QEMU with KVM, 2 virtual CPUs. Five boots of each image, alternating. Serial logs: [`docs/results/qnx-instant-on-ab.txt`](results/qnx-instant-on-ab.txt).

| All 5 boots of each image | Instant on | Started after the normal boot |
|---|---|---|
| First full compression done | **before the disks are mounted**, in every boot | after "Startup complete", in every boot |
| State after boot | compressing, no fault, 0 rule violations | same |

In every instant-on boot the console shows Elvrek's first compression done around the moment the disk driver starts and before any file system is mounted, and the piston keeps working while the file systems are mounted, the network comes up and the SSH server starts. In every comparison boot it comes only after the whole system has booted.

**Times are not published.** We measured how long each step took, but those times depend mostly on how fast QNX itself boots, and on 28 Nov 2026 the hackathon organisers asked that entries do not publish performance evaluations of QNX software (licence section 4.6(i)). So this page reports the order of events, which is what our design controls, and the boot milestones are withheld from the logs.

## What this does and does not show

* It runs in an emulator, and the hardware is the physics model. A real device would also home the piston and check its sensors before the first push.
* With the programs started from the boot image, `elvrekd` starts before `slogger2`, so it runs without its system log; the status device is unaffected. A product would start the logger first.
* START is pressed by `elv-ctl boot` as soon as the device answers. A real crew positions the device first; what this shows is that the controller is never the reason they wait.

## Run it

```sh
make -C qnx                                   # target binaries
bash qnx/instant-on/build.sh                  # ~/elvrek-instant
bash qnx/instant-on/build-conventional.sh     # ~/elvrek-conventional
bash build/restart_guest.sh                   # stop any running guest first
bash qnx/instant-on/boot-time.sh 5            # five boots of the instant image
```
