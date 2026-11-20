# Elvrek demo video: transcript

**0:01  Intro**

When a heart stops, every second without compressions counts. This is Elvrek. A real-time safety controller for mechanical CPR, built on QNX 8.0.

**0:13  Landing page**

Mechanical CPR machines never get tired, but their compressions, the breaths and the defibrillator usually run on separate clocks. Every extra second of pause before a shock lowers the odds of survival. Elvrek puts all three on one clock, ticking a thousand times a second. The AI only makes suggestions.

**0:33  The system**

Here is the cycle. The charger fills while the piston keeps working. The arm parks at full recoil only for the shock itself, a trusted check confirms the rhythm, and compressions resume on the next tick. The machine's part of that pause is about eight tenths of a second. The crew's time is counted separately.

**0:51  Why QNX**

Why QNX? Every job here is its own small process. The safety process runs at the top priority on a timer pulse. The rhythm classifier runs unprivileged, and QNX refuses it the piston and the shock line. If it crashes, it is restarted. The safety process never is. A hardware watchdog lifts the cup instead.

**1:12  On the QNX target: boot**

Now the real thing. QNX 8.0 boots in QEMU, with the whole stack built with qcc. The piston and chest are a physics model, behind the same interface a real motor driver will use.

**1:25  On the QNX target: the classifier attacks**

First, the classifier attacks. It tries the piston, the shock line and every other forbidden path. Seven of seven are refused by the operating system.

**1:36  On the QNX target: shock and return of circulation**

Next, a shockable rhythm. Compressions run while the charger fills. The crew pre-authorises, the window opens, the shock fires, and compressions are back in under a second. Then carbon dioxide rises, a trusted sign of returning circulation, and the piston parks.

**1:53  On the QNX target: safety process killed**

Last, we kill the safety process in the middle of a stroke. The driver's own watchdog lifts the cup to full recoil, and nothing restarts the controller.

**2:02  Evidence**

Every result is on the evidence page. Eleven of eleven fault cases pass on QNX, two runs in a row. It also shows what is not good enough yet, like our rhythm check on real arrest ECG. We say so plainly.

**2:17  Recorded run**

You can replay a recorded run yourself, from the first compression to the moment circulation returns.

**2:23  FAQ and mobile**

Questions are answered in the FAQ, and the whole site works on a phone.

**2:30  Closing**

Elvrek is open source, and it is not a medical device. Next stop, a Raspberry Pi 5 and a training manikin. Save lives, smarter.
