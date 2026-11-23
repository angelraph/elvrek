# Elvrek demo video: transcript

**0:01  Intro**

When a heart stops, every second without compressions counts. This is Elvrek. A real-time safety controller for mechanical CPR, built on QNX 8.0.

**0:13  Landing page**

Today a crew runs a CPR machine and a separate defibrillator, each on its own clock, and every handover between them is a pause. Elvrek puts compressions, breaths and the shock on one clock, ticking a thousand times a second. The AI only makes suggestions.

**0:30  Why QNX**

On QNX, every job is its own small process. The safety process runs at the top priority on a timer pulse. The rhythm classifier runs unprivileged, and QNX refuses it the piston and the shock line.

**0:44  On the QNX target: boot**

Now the real thing. QNX 8.0 boots in QEMU, with the whole stack built with qcc. The piston and chest are a physics model, behind the interface a real motor driver will use.

**0:57  On the QNX target: the classifier attacks**

First, the classifier attacks. It tries the piston, the shock line and every other forbidden path. Seven of seven are refused.

**1:06  On the QNX target: shock read through the compressions**

Next, a shockable rhythm. The charger fills while the piston keeps working, and the controller reads the rhythm through its own compressions. It is sure, so there is no pause to look. The shock fires, compressions are back within milliseconds, then carbon dioxide rises and the piston parks.

**1:24  Crew console on QNX Screen**

This is the crew's screen, drawn with QNX Screen on the target. It only reads. The crew sees charging, then shock delivered. The pause was too short to show.

**1:36  On the QNX target: safety process killed**

Now we kill the safety process in the middle of a stroke. The driver's own watchdog lifts the cup, and nothing restarts the controller.

**1:44  Instant on**

Elvrek also starts from the boot image itself. The first compression is done 1.4 seconds after the kernel starts, before the disks or the network are up.

**1:55  One clock against two**

We ran the same arrest the usual way too, a CPR device plus a separate AED. The pause before the shock was twelve seconds, and five even when the crew pre-charges. With Elvrek it was three tenths of a second, the crew's own press.

**2:11  The evidence**

Every claim is checked. Twelve of twelve fault cases pass on QNX. A kernel trace finds no tick whose work ran past one millisecond. And thirty one safety requirements are traced to code and tests, automatically.

**2:25  Closing**

Elvrek is open source, and it is not a medical device. Next stop, a Raspberry Pi 5 and a training manikin. Save lives, smarter.
