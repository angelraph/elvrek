# YouTube upload

**Title**

Elvrek: a real-time safety controller for mechanical CPR on QNX 8.0 (QNX Global Hackathon demo)

**Description**

When a heart stops, every second without chest compressions counts, and the pause right before a shock matters most. Elvrek is the safety controller for a chest compression robot, built on QNX 8.0. One QNX process wakes a thousand times a second and owns the piston, the timing of every breath, the defibrillator charger and the shock line.

What you see in this video, all recorded from QNX 8.0 running in QEMU:

* The stack boots and every job runs as its own small process, at its own priority.
* The AI rhythm classifier tries every forbidden action. Under an enforced QNX security policy, 7 of 7 are refused.
* A shockable rhythm: the charger fills while the piston keeps working, the controller reads the rhythm through its own compressions, and the shock goes through with a few milliseconds of hands-off time.
* The crew console, drawn with QNX Screen. It only reads.
* We kill the safety process mid stroke. The motor driver's own watchdog lifts the cup, and nothing restarts the controller behind your back.
* Instant on: compressing from the boot image itself, before the disks are even mounted.
* The same arrest run the usual way, a CPR device plus a separate AED: 12.3 s of hands-off before the shock, against 0.3 s with Elvrek.
* 12 of 12 fault cases on QNX, a kernel trace with no tick of work past 1 ms, and 31 of 31 requirements traced to code and tests.

Honest notes: the piston and chest are a physics model behind the same interface a real motor driver will use. Some QNX clips are sped up or slowed down, and each one says so on screen. Elvrek is a research controller, not a medical device, and must never be connected to a person.

Chapters
0:00 Intro
0:12 One clock for compressions, breaths and the shock
0:29 Why QNX
0:43 QNX boots, the AI classifier attacks
1:06 A shock read through the compressions
1:24 Crew console, then killing the safety process
1:43 Instant on from the boot image
1:54 Same arrest, the usual way
2:10 The evidence
2:25 What's next

Code and every raw result: https://github.com/angelraph/elvrek
Website: https://elvrek.vercel.app

Narration voice: Microsoft Edge text to speech. Music: original, made for this video.

#QNX #RealTime #CPR #EmbeddedSystems #Robotics

**Tags**

QNX, QNX 8.0, real-time operating system, RTOS, mechanical CPR, defibrillator, embedded systems, safety-critical, medical robotics, hackathon
