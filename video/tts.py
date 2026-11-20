"""Voice-over: one mp3 per scene plus sentence timings for captions."""
import asyncio, json, pathlib, subprocess
import edge_tts, imageio_ffmpeg

HERE = pathlib.Path(__file__).parent
OUT = HERE / "build" / "voice"
OUT.mkdir(parents=True, exist_ok=True)
FF = imageio_ffmpeg.get_ffmpeg_exe()


def duration(path):
    r = subprocess.run([FF, "-hide_banner", "-i", str(path)], capture_output=True, text=True)
    for ln in r.stderr.splitlines():
        if "Duration:" in ln:
            h, m, s = ln.split("Duration:")[1].split(",")[0].strip().split(":")
            return int(h) * 3600 + int(m) * 60 + float(s)
    return 0.0


async def one(scene, voice, rate):
    mp3 = OUT / f"{scene['id']}.mp3"
    for attempt in range(5):
        sents = []
        try:
            com = edge_tts.Communicate(scene["text"], voice, rate=rate, boundary="SentenceBoundary")
            with open(mp3, "wb") as f:
                async for ch in com.stream():
                    if ch["type"] == "audio":
                        f.write(ch["data"])
                    elif ch["type"] in ("SentenceBoundary", "WordBoundary"):
                        sents.append({"t": ch["offset"] / 1e7, "d": ch["duration"] / 1e7, "text": ch["text"]})
            break
        except edge_tts.exceptions.NoAudioReceived:
            print("retry", scene["id"]); await asyncio.sleep(3)
    return {"id": scene["id"], "file": str(mp3), "dur": duration(mp3), "sentences": sents}


async def main():
    cfg = json.loads((HERE / "script.json").read_text(encoding="utf-8"))
    res = [await one(s, cfg["voice"], cfg["rate"]) for s in cfg["scenes"]]
    (OUT / "timings.json").write_text(json.dumps(res, indent=1), encoding="utf-8")
    tot = 0
    for r in res:
        tot += r["dur"]
        print(f"{r['id']:11s} {r['dur']:5.1f}s  {len(r['sentences'])} sentences")
    print(f"voice total {tot:.1f}s")

asyncio.run(main())
