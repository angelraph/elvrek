"""Joins scene clips with transitions, builds the music, effects and voice
mix, and writes the final videos, captions and transcript."""
import json, math, pathlib, subprocess
import numpy as np
import imageio_ffmpeg

HERE = pathlib.Path(__file__).parent
B = HERE / "build"
FF = imageio_ffmpeg.get_ffmpeg_exe()
SR = 48000
ORDER = ["intro", "home", "technology", "boot", "attack", "shock", "console", "watchdog",
         "instant", "workflow", "proof", "outro"]
# transition INTO each scene (from the previous one)
XF = {"home": ("fade", 0.8), "technology": ("smoothleft", 0.6),
      "boot": ("fadeblack", 0.9), "attack": ("fade", 0.5), "shock": ("fade", 0.5),
      "console": ("fade", 0.6), "watchdog": ("fade", 0.6), "instant": ("fadeblack", 0.8),
      "workflow": ("smoothleft", 0.6), "proof": ("smoothleft", 0.6), "outro": ("fadeblack", 1.0)}
LEAD = {"intro": 1.6, "outro": 0.8}
META = json.loads((B / "scenes.json").read_text())
TIM = {s["id"]: s for s in json.loads((B / "voice" / "timings.json").read_text())}


def decode(path):
    raw = subprocess.run([FF, "-v", "error", "-i", str(path), "-f", "f32le", "-ac", "1", "-ar", str(SR), "-"],
                         capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).copy()


def timeline():
    starts, t = {}, 0.0
    for i, s in enumerate(ORDER):
        if i:
            t -= XF[s][1]
        starts[s] = t
        t += META[s]["frames"] / 30.0
    return starts, t


# ---------------- music ----------------
def note(n):  # midi to Hz
    return 440.0 * 2 ** ((n - 69) / 12)


def music(total):
    N = int(total * SR); t = np.arange(N) / SR
    out = np.zeros(N)
    chords = [[57, 60, 64], [53, 57, 60], [48, 52, 55], [55, 59, 62]]  # Am F C G
    bar = 8.0
    for ci in range(int(total // bar) + 2):
        ch = chords[ci % 4]; t0 = ci * bar
        a, b = int(max(0, t0 - 1.5) * SR), min(N, int((t0 + bar + 1.5) * SR))
        if a >= N: break
        tt = t[a:b] - t0
        env = np.clip((tt + 1.5) / 2.5, 0, 1) * np.clip((bar + 1.5 - tt) / 2.5, 0, 1)
        env = env ** 1.5
        for n in ch:
            for det in (-0.0025, 0.0025):
                f = note(n + 12) * (1 + det)
                for h, amp in ((1, 1), (2, .35), (3, .15)):
                    out[a:b] += env * amp * 0.05 * np.sin(2 * np.pi * f * h * tt + h * 0.7 + det * 900)
        out[a:b] += env * 0.09 * np.sin(2 * np.pi * note(ch[0] - 12) * tt)
    # soft pluck on the beat at 110 a minute, the compression rate
    beat = 60 / 110.0
    k = 0
    while k * beat < total:
        t0 = k * beat; a = int(t0 * SR); b = min(N, a + int(0.5 * SR))
        ch = chords[int(t0 // bar) % 4]
        f = note(ch[k % 3] + 24)
        tt = t[a:b] - t0
        out[a:b] += 0.035 * np.exp(-tt * 9) * np.sin(2 * np.pi * f * tt) * (1 if k % 2 == 0 else 0.6)
        k += 1
    # gentle slow lowpass to round it off
    y = np.zeros_like(out); al = 0.08; acc = 0.0
    for i in range(0, N, 4096):
        seg = out[i:i + 4096]
        for j in range(len(seg)):
            acc += al * (seg[j] - acc); seg[j] = acc
        y[i:i + 4096] = seg
    return y


def sfx_click():
    n = int(0.06 * SR); tt = np.arange(n) / SR
    rng = np.random.default_rng(3)
    return (0.25 * np.exp(-tt * 90) * np.sin(2 * np.pi * 1900 * tt) +
            0.12 * np.exp(-tt * 140) * rng.standard_normal(n))


def sfx_whoosh(d):
    n = int(d * SR); tt = np.arange(n) / SR
    rng = np.random.default_rng(7); x = rng.standard_normal(n)
    env = np.sin(np.pi * tt / d) ** 2
    y = np.zeros(n); acc = 0.0
    for i in range(n):
        al = 0.02 + 0.25 * env[i]
        acc += al * (x[i] - acc); y[i] = acc
    return 0.10 * env * y / (np.abs(y).max() + 1e-9)


def sfx_thump():
    n = int(0.9 * SR); tt = np.arange(n) / SR
    f = 70 * np.exp(-tt * 2.5) + 38
    ph = 2 * np.pi * np.cumsum(f) / SR
    return 0.55 * np.exp(-tt * 5) * np.sin(ph)


def place(buf, x, t, g=1.0):
    a = int(t * SR)
    if a >= len(buf): return
    b = min(len(buf), a + len(x)); buf[a:b] += g * x[:b - a]


def main():
    starts, total = timeline()
    N = int((total + 0.5) * SR)
    voice = np.zeros(N); fx = np.zeros(N); vstart = {}
    for s in ORDER:
        v = decode(TIM[s]["file"]); t0 = starts[s] + LEAD.get(s, 0.7); vstart[s] = t0
        place(voice, v, t0)
    for i, s in enumerate(ORDER):
        if i:
            d = XF[s][1]; place(fx, sfx_whoosh(d + 0.3), starts[s] - 0.15, 0.9)
        for c in META[s].get("clicks", []):
            place(fx, sfx_click(), starts[s] + c, 0.8)
    if META["shock"].get("shockT"):
        place(fx, sfx_thump(), starts["shock"] + META["shock"]["shockT"], 0.9)
    m = music(N / SR)[:N]
    m = m / (np.abs(m).max() + 1e-9) * 0.30
    # duck the music under the voice
    env = np.convolve(np.abs(voice), np.ones(SR // 10) / (SR // 10), mode="same")
    duck = 1 - 0.55 * np.clip(env / 0.03, 0, 1)
    sm = np.zeros_like(duck); acc = 1.0
    for i in range(0, N, 480):
        acc += 0.08 * (duck[i:i + 480].min() - acc); sm[i:i + 480] = acc
    mix = voice * 0.95 + m * sm + fx
    fade = int(1.4 * SR); mix[-fade:] *= np.linspace(1, 0, fade); mix[:int(0.3 * SR)] *= np.linspace(0, 1, int(0.3 * SR))
    mix = mix / max(1.0, np.abs(mix).max() / 0.97)
    st = np.stack([mix, mix], 1).astype(np.float32)
    (B / "mix.f32").write_bytes(st.tobytes())

    # video: xfade chain
    ins, fc, prev, acc = [], [], "[0:v]", META[ORDER[0]]["frames"] / 30.0
    for s in ORDER:
        ins += ["-i", str(B / "scenes" / f"{s}.mp4")]
    for i, s in enumerate(ORDER[1:], 1):
        kind, d = XF[s]; off = acc - d
        lab = f"[v{i}]"
        fc.append(f"{prev}[{i}:v]xfade=transition={kind}:duration={d}:offset={off:.3f}{lab}")
        prev = lab; acc = off + META[s]["frames"] / 30.0
    fc.append(f"{prev}fade=t=out:st={acc - 1.2:.3f}:d=1.2,format=yuv420p[vout]")
    ai = len(ORDER)
    cmd = [FF, "-y", "-v", "error", *ins, "-f", "f32le", "-ar", str(SR), "-ac", "2", "-i", str(B / "mix.f32"),
           "-filter_complex", ";".join(fc), "-map", "[vout]", "-map", f"{ai}:a",
           "-c:v", "libx264", "-preset", "slow", "-crf", "17", "-r", "30", "-movflags", "+faststart",
           "-c:a", "aac", "-b:a", "192k", "-shortest", str(B / "elvrek-demo.mp4")]
    subprocess.run(cmd, check=True)

    # captions
    caps = []
    for s in ORDER:
        sents = TIM[s]["sentences"]
        for j, x in enumerate(sents):
            a = vstart[s] + x["t"]
            b = vstart[s] + (sents[j + 1]["t"] if j + 1 < len(sents) else TIM[s]["dur"] + 0.2)
            caps.append((a, b - 0.05, x["text"]))
    def ts(x, sep):
        h = int(x // 3600); m_ = int(x % 3600 // 60); s_ = x % 60
        return f"{h:02d}:{m_:02d}:{s_:06.3f}".replace(".", sep) if sep == "," else f"{h}:{m_:02d}:{s_:05.2f}"
    srt = "\n".join(f"{i}\n{ts(a, ',')} --> {ts(b, ',')}\n{t}\n" for i, (a, b, t) in enumerate(caps, 1))
    (B / "elvrek-demo.srt").write_text(srt, encoding="utf-8")
    ass = ["[Script Info]", "ScriptType: v4.00+", "PlayResX: 1920", "PlayResY: 1080", "WrapStyle: 0", "",
           "[V4+ Styles]",
           "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding",
           "Style: Cap,Segoe UI Semibold,40,&H00FFFFFF,&H00FFFFFF,&H70000000,&H70000000,0,0,0,0,100,100,0,0,3,16,0,2,330,330,54,1",
           "", "[Events]", "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text"]
    for a, b, t in caps:
        ass.append(f"Dialogue: 0,{ts(a, '.')},{ts(b, '.')},Cap,,0,0,0,,{{\\fad(180,140)}}{t}")
    (B / "captions.ass").write_text("\n".join(ass), encoding="utf-8")
    subprocess.run([FF, "-y", "-v", "error", "-i", "elvrek-demo.mp4", "-vf", "ass=captions.ass",
                    "-c:v", "libx264", "-preset", "slow", "-crf", "17", "-c:a", "copy", "-movflags", "+faststart",
                    "elvrek-demo-captions.mp4"], check=True, cwd=B)
    cfg = json.loads((HERE / "script.json").read_text(encoding="utf-8"))
    names = {"intro": "Intro", "home": "Landing page", "technology": "Why QNX",
             "boot": "On the QNX target: boot", "attack": "On the QNX target: the classifier attacks",
             "shock": "On the QNX target: shock read through the compressions",
             "console": "Crew console on QNX Screen",
             "watchdog": "On the QNX target: safety process killed", "instant": "Instant on",
             "workflow": "One clock against two", "proof": "The evidence", "outro": "Closing"}
    md = ["# Elvrek demo video: transcript", ""]
    for sc in cfg["scenes"]:
        a = vstart[sc["id"]]
        md += [f"**{int(a // 60)}:{int(a % 60):02d}  {names[sc['id']]}**", "", sc["text"], ""]
    (B / "transcript.md").write_text("\n".join(md), encoding="utf-8")
    print(f"total {total:.1f} s")


if __name__ == "__main__":
    main()
