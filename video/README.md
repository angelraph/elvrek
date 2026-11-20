# How the demo film was made

The 2:41 demo ([YouTube](https://youtu.be/lBU1zjZX8Ok)) is built entirely from this folder. Nothing in it is mocked up: the website clips are the live site, and every QNX clip replays real output recorded from QNX 8.0 in QEMU.

## Pipeline

| Step | File | What it does |
|---|---|---|
| Script | [`script.json`](script.json) | Twelve scenes of narration, about 400 words |
| Voice | [`tts.py`](tts.py) | Generates the voice-over with Microsoft Edge text to speech and records the timing of every sentence, so captions and on-screen moments line up with the words |
| Real QNX output | [`capture/capture_qnx.sh`](capture/capture_qnx.sh), [`capture/demo_body.sh`](capture/demo_body.sh) | Reboots the QNX guest, records the serial boot log, then runs the real stack on the target (the classifier attack, a full shock with return of circulation, killing the safety process) with every line timestamped. The results are [`capture/boot.tsv`](capture/boot.tsv) and [`capture/demo.tsv`](capture/demo.tsv) |
| Frames | [`rec.py`](rec.py), [`scenes.py`](scenes.py), [`overlay.js`](overlay.js), [`scenes/`](scenes/) | Drives headless Chromium one frame at a time: the page clock, CSS animations and scrolling all advance exactly 1/30 s per frame, so motion is perfectly smooth. The cursor, clicks and section labels are drawn on top. The QNX scenes replay the captured output in a terminal with a live panel |
| Edit | [`assemble.py`](assemble.py) | Joins the scenes with fades and slides, composes an original ambient score with a soft pulse at 110 beats a minute (the compression rate), adds click and transition sounds, ducks the music under the voice, and writes the video, the subtitles and the transcript |

## Honest labels

Every QNX scene carries a badge saying it is real output and how it is played back: the boot is shown in real time, the commands are paced to read, the shock run is sped up during compressions and slowed down through the shock window. The narration says that the piston and chest are a physics model.

## Outputs

The final files are written to `final/`: the video with and without burned-in captions, the subtitle file, the transcript ([`final/Elvrek-demo-transcript.md`](final/Elvrek-demo-transcript.md)) and the thumbnails. The video files themselves are not stored in git.

## Rebuilding

```sh
python tts.py                              # voice and timings
bash capture/capture_qnx.sh                # in WSL, with the QNX guest set up (optional: the captures are committed)
python scenes.py intro home system technology boot attack shock watchdog evidence simulator more outro
python assemble.py
```
