"""Renders one scene clip: python scenes.py <scene_id> [...]"""
import json, math, pathlib, sys
from playwright.sync_api import sync_playwright
import rec

HERE = pathlib.Path(__file__).parent
BUILD = HERE / "build"
SITE = "https://elvrek.vercel.app"
LEAD, TAIL = 0.7, 0.8
TIM = {s["id"]: s for s in json.loads((BUILD / "voice" / "timings.json").read_text())}

FIND_JS = """(q) => {
  const els = [...document.querySelectorAll(q.sel || 'h1,h2,h3,h4,button,summary,a,.stats,.tabs')];
  const el = q.text ? els.find(e => e.offsetParent !== null && e.textContent.replace(/\\s+/g,' ').trim().toLowerCase().includes(q.text.toLowerCase()))
                    : els.find(e => e.offsetParent !== null);
  if (!el) return null;
  const r = el.getBoundingClientRect();
  return {x: r.left + r.width/2, y: r.top + r.height/2, top: r.top + scrollY,
          max: document.documentElement.scrollHeight - innerHeight};
}"""
CLICK_JS = """(q) => {
  const els = [...document.querySelectorAll(q.sel || 'button,summary,a')];
  const el = q.text ? els.find(e => e.offsetParent !== null && e.textContent.replace(/\\s+/g,' ').trim().toLowerCase().includes(q.text.toLowerCase()))
                    : els.find(e => e.offsetParent !== null);
  if (el) el.click();
}"""


def io(x):
    x = max(0.0, min(1.0, x))
    return 4 * x ** 3 if x < .5 else 1 - (-2 * x + 2) ** 3 / 2


def dur_of(sid, extra=0.0):
    return TIM[sid]["dur"] + LEAD + TAIL + extra


def chip_state(t, k, label, dur):
    o = min(1, max(0, (t - 0.35) / 0.45))
    o = min(o, max(0, (dur - 0.4 - t) / 0.4))
    return {"k": k, "t": label, "o": io(o)}


# ---------------- website tours ----------------
TOURS = {
 "home": ("01", "Home", "/", [
    ("hold", 2.4), ("scroll", {"text": "The pause before a shock"}, 2.2), ("hold", 2.6),
    ("scroll", {"sel": ".stats"}, 2.0), ("hold", 3.0),
    ("scroll", {"text": "Compress. Breathe."}, 2.0), ("hold", 2.2),
    ("move", {"sel": ".nav .drop > button"}, 1.0), ("hold_fixed", 0.6),
    ("move", {"sel": ".nav a[href='/technology']"}, 0.6), ("click", {"sel": ".nav a[href='/technology']", "nav": True}, 0.5)]),
 "system": ("02", "The system", "/system", [
    ("hold", 1.6), ("scroll", {"text": "The compression cycle"}, 2.0), ("hold", 2.0),
    ("scroll", {"text": "Charge while compressing"}, 2.0), ("hold", 3.6),
    ("scroll", {"text": "Machine time and human time"}, 2.0), ("hold", 2.0),
    ("move", {"sel": ".nav .drop > button"}, 0.9), ("hold_fixed", 0.6),
    ("move", {"sel": ".nav a[href='/technology']"}, 0.6), ("click", {"sel": ".nav a[href='/technology']", "nav": True}, 0.5)]),
 "technology": ("02", "Why QNX", "/technology", [
    ("hold", 1.5), ("scroll", {"text": "Small processes, clear jobs"}, 2.0), ("hold", 3.4),
    ("scroll", {"text": "The AI may suggest"}, 2.0), ("hold", 5.0),
    ("scroll", {"text": "Eight states, one owner"}, 2.0), ("hold", 3.5)]),
 "evidence": ("05", "The evidence", "/evidence", [
    ("hold", 1.3), ("move", {"sel": "[role=tab]", "text": "On the QNX target"}, 1.0),
    ("click", {"sel": "[role=tab]", "text": "On the QNX target"}, 0.4), ("hold", 0.7),
    ("scroll", {"sel": "#target table"}, 1.6, -160), ("hold", 1.2),
    ("scroll", {"sel": ".tabs"}, 1.1, -200), ("move", {"sel": "[role=tab]", "text": "Real ECG"}, 0.6),
    ("click", {"sel": "[role=tab]", "text": "Real ECG"}, 0.4), ("hold", 2.5)]),
 "simulator": ("06", "Replay a run", "/simulator", [
    ("hold", 0.8), ("scroll", {"sel": "[data-player]"}, 1.2, -170),
    ("move", {"sel": "[data-player] .play"}, 0.9), ("click", {"sel": "[data-player] .play"}, 0.4), ("hold", 3.0)]),
}


def tour(sid, out):
    k, label, path, steps = TOURS[sid]
    D = dur_of(sid)
    with sync_playwright() as pw:
        p = rec.Page(pw, url=SITE + path)
        p.js("() => window.scrollTo(0,0)")
        p.frame  # noqa
        fixed = sum(s[2] for s in steps if s[0] in ("scroll", "move", "click")) + \
                sum(s[1] for s in steps if s[0] == "hold_fixed")
        flex = sum(s[1] for s in steps if s[0] == "hold")
        scale = max(0.3, (D - fixed) / flex) if flex else 1
        tl, t = [], 0.0
        for s in steps:
            d = s[1] * scale if s[0] == "hold" else (s[1] if s[0] == "hold_fixed" else s[2])
            tl.append((t, d, s)); t += d
        enc = rec.Encoder(out)
        n = int(round(max(D, t) * rec.FPS))
        y = 0.0; cur = None; cur_o = 0.0; clicks = []
        idx = -1; y0 = y1 = 0; c0 = c1 = None; rip = None
        for f in range(n):
            tt = f / rec.FPS
            while idx + 1 < len(tl) and tt >= tl[idx + 1][0]:
                idx += 1; st, dd, s = tl[idx]
                if s[0] == "scroll":
                    q = p.js(FIND_JS, s[1]); off = s[3] if len(s) > 3 else -150
                    y0 = y; y1 = max(0, min(q["max"], q["top"] + off)) if q else y
                elif s[0] == "move":
                    q = p.js(FIND_JS, s[1])
                    c0 = cur or (1450.0, 820.0); c1 = (q["x"], q["y"]) if q else c0
                elif s[0] == "click":
                    q = p.js(FIND_JS, s[1])
                    if q: rip = (q["x"], q["y"], st); cur = (q["x"], q["y"])
                    clicks.append(st)
                    if not s[1].get("nav"):
                        p.js(CLICK_JS, s[1])
            st, dd, s = tl[idx] if idx >= 0 else (0, 1, ("hold", 1))
            u = (tt - st) / dd if dd else 1
            if s[0] == "scroll":
                y = y0 + (y1 - y0) * io(u)
            if s[0] == "move":
                cur = (c0[0] + (c1[0] - c0[0]) * io(u), c0[1] + (c1[1] - c0[1]) * io(u))
                p.pg.mouse.move(cur[0], cur[1])
            if cur is not None:
                cur_o = min(1.0, cur_o + 1 / (0.3 * rec.FPS))
            state = {"chip": chip_state(tt, k, label, n / rec.FPS)}
            if cur is not None:
                press = 1.0
                if rip and 0 <= tt - rip[2] < 0.18: press = 0.86
                state["cursor"] = {"x": cur[0], "y": cur[1], "o": cur_o, "s": press}
            if rip and 0 <= tt - rip[2] < 0.55:
                state["ripple"] = {"x": rip[0], "y": rip[1], "p": (tt - rip[2]) / 0.55}
            p.frame(enc, state, scroll=y)
        n = enc.close(); p.close()
        return {"frames": n, "clicks": clicks}


# ---------------- QNX scenes ----------------
def sec_lines(rows):
    return [r["l"] for r in rows]

def qnx_data():
    cap = HERE / "capture"
    boot = [l.rstrip("\n").split("\t", 1) for l in open(cap / "boot.tsv", encoding="utf-8")]
    b0 = float(boot[0][0])
    bootl = [{"t": float(a) - b0, "l": b.replace("\x1b[?7h", "")} for a, b in boot if b.strip()]
    rows = [l.rstrip("\n").split("\t", 1) for l in open(cap / "demo.tsv", encoding="utf-8")]
    sec, cur = {}, None
    for ts, l in rows:
        if l.startswith("@@scene"):
            cur = l.split()[1]; sec[cur] = []; continue
        if l.startswith("@@end"): break
        sec[cur].append(l)

    def wrap(l, w=92):
        out = []
        while len(l) > w:
            cut = l.rfind(" ", 0, w)
            cut = cut if cut > 20 else w
            out.append(l[:cut]); l = "    " + l[cut:].lstrip()
        return out + [l]

    # boot commands, paced to read
    bd, t = [], 0.0
    for l in sec["boot"]:
        if l.startswith("$ "): t += 0.75
        elif l.startswith("tick="): l = l[:88] + " ..."
        for w in wrap(l):
            bd.append({"t": t, "l": w}); t += 0.0 if l.startswith("$ ") else 0.16
        if l.startswith("$ "): t += 0.6
    shock = []
    pend = None
    for l in sec["shock"]:
        if l.startswith("tick="):
            tk = int(l.split()[0].split("=")[1])
            if pend: shock.append({"tick": tk - 1, "crew": pend}); pend = None
            shock.append({"tick": tk, "l": l})
        elif l.startswith("crew:"):
            pend = l
    wd, t = [], 0.3
    for l in sec["watchdog"]:
        if l.startswith("$ "): t += 0.55
        for w in wrap(l):
            wd.append({"t": t, "l": w})
        t += 0.75 if l.startswith("$ ") else 0.35
    return {"boot": bootl, "bootDemo": bd, "attack": sec["attack"], "shock": shock, "wd": wd}


def qnx(sid, out):
    D = qnx_data()
    dur = dur_of(sid)
    if sid == "shock":
        sh = [r for r in D["shock"] if "l" in r]
        val = lambda r, k: (" " + r["l"] + " ").split(" " + k + "=")[1].split(" ")[0]
        t0 = sh[0]["tick"]
        tch = next(r["tick"] for r in sh if val(r, "charge") == "1")
        i1 = next(i for i, r in enumerate(sh) if val(r, "shocks") == "1")
        tshock = (sh[i1 - 1]["tick"] + sh[i1]["tick"]) // 2      # between the two samples around it
        tros = next((r["tick"] for r in sh if val(r, "state") == "ROSC_HOLD"), None)
        tend = sh[-1]["tick"]
        L = LEAD
        segs = [[0, t0], [L + 1.7, tch - 1500], [L + 7.6, tshock - 700], [L + 10.0, tshock + 300],
                [L + 12.4, tshock + 3000]]
        if tros:
            segs += [[L + 15.6, tros + 400]]
        segs += [[dur, max(tend, segs[-1][1] + 1)]]
        D["segs"] = segs
        a, b = segs[2], segs[3]
        D["shockT"] = a[0] + (tshock - a[1]) / (b[1] - a[1]) * (b[0] - a[0])
    if sid == "watchdog":
        tl = next(l for l in sec_lines(D["wd"]) if l.startswith("true_depth_mm="))
        D["wdDepth"] = float(tl.split()[0].split("=")[1])
    html = (HERE / "scenes" / "qnx.html").read_text(encoding="utf-8")
    with sync_playwright() as pw:
        p = rec.Page(pw, html=html)
        p.js("([s, d]) => window.__qnx.init(s, d)", [sid, D])
        enc = rec.Encoder(out)
        n = int(round(dur * rec.FPS))
        for f in range(n):
            tt = f / rec.FPS
            p.js("t => window.__qnx.render(t)", tt)
            p.frame(enc, {"chip": chip_state(tt + (0 if sid == "boot" else 5), "03", "On the QNX target", 999)})
        n = enc.close(); p.close()
    return {"frames": n, "clicks": [], "shockT": D.get("shockT")}


# ---------------- instant on, workflow, proof ----------------
def cards(sid, out):
    html = (HERE / "scenes" / "cards.html").read_text(encoding="utf-8")
    s = [x["t"] for x in TIM[sid]["sentences"]]
    L = LEAD; dur = dur_of(sid, 0.6)
    if sid == "instant":
        T = {"run": L + s[1] - 0.6, "step": 0.62}
        chip = ("05", "Instant on")
    elif sid == "workflow":
        T = {"a": L + s[1] + 0.9, "b": L + s[1] + 3.0, "c": L + s[2]}
        chip = ("06", "One clock against two")
    else:
        T = {"a": L + s[1], "b": L + s[2], "c": L + s[3]}
        chip = ("07", "The evidence")
    with sync_playwright() as pw:
        p = rec.Page(pw, html=html)
        p.js("([m, T]) => window.__cards.init(m, T)", [sid, T])
        enc = rec.Encoder(out)
        n = int(round(dur * rec.FPS))
        for f in range(n):
            tt = f / rec.FPS
            p.js("t => window.__cards.render(t)", tt)
            p.frame(enc, {"chip": chip_state(tt, chip[0], chip[1], dur)})
        n = enc.close(); p.close()
    return {"frames": n, "clicks": []}


# ---------------- crew console: the replayed target log ----------------
CONSOLE_HTML = (HERE / "scenes" / "console.html")


def console(sid, out):
    import subprocess, imageio_ffmpeg
    dur = dur_of(sid, 0.4)
    bgs = []
    with sync_playwright() as pw:
        p = rec.Page(pw, html=CONSOLE_HTML.read_text(encoding="utf-8"), fake_clock=False)
        for i, speed in enumerate((False, True)):
            if speed:
                p.js("() => document.getElementById('sp').style.display='inline-block'")
            p.js("s => window.__ov.render(s)", {"chip": {"k": "04", "t": "Crew console", "o": 1}})
            f = BUILD / f"console_bg{i}.png"; p.pg.screenshot(path=str(f)); bgs.append(f)
        p.close()
    src = HERE / "capture" / "console.mp4"
    # console log: charging starts at 120.0 s, ready at 127.2 s, shock delivered at 128.1 s
    a0, a1 = 116.0, 121.6           # compressing, then charging begins, real time
    b1 = 127.4                      # charging, sped up
    sp = 3.0
    c1 = b1 + (dur - (a1 - a0) - (b1 - a1) / sp) + 0.2
    t_fast0 = a1 - a0; t_fast1 = t_fast0 + (b1 - a1) / sp
    ff = imageio_ffmpeg.get_ffmpeg_exe()
    fc = (f"[2:v]split=3[s1][s2][s3];"
          f"[s1]trim={a0}:{a1},setpts=PTS-STARTPTS[v1];"
          f"[s2]trim={a1}:{b1},setpts=(PTS-STARTPTS)/{sp}[v2];"
          f"[s3]trim={b1}:{c1:.3f},setpts=PTS-STARTPTS[v3];"
          f"[v1][v2][v3]concat=n=3:v=1:a=0,fps=30,scale=1398:838:flags=lanczos[con];"
          f"[0:v][1:v]overlay=0:0:enable='between(t,{t_fast0:.3f},{t_fast1:.3f})'[bg];"
          f"[bg][con]overlay=261:197:shortest=1,format=yuv420p[v]")
    subprocess.run([ff, "-y", "-loglevel", "error",
                    "-loop", "1", "-framerate", "30", "-t", f"{dur:.3f}", "-i", str(bgs[0]),
                    "-loop", "1", "-framerate", "30", "-t", f"{dur:.3f}", "-i", str(bgs[1]), "-i", str(src),
                    "-filter_complex", fc, "-map", "[v]", "-t", f"{dur:.3f}", "-c:v", "libx264", "-preset", "slow",
                    "-crf", "15", "-r", "30", str(out)], check=True)
    return {"frames": int(round(dur * 30)), "clicks": []}


# ---------------- brand, intro and outro ----------------
def brand(sid, out):
    html = (HERE / "scenes" / "brand.html").read_text(encoding="utf-8")
    s = [x["t"] for x in TIM[sid]["sentences"]]
    if sid == "intro":
        L = 1.6; dur = TIM[sid]["dur"] + L + 1.3
        T = {"flat": L + 0.85, "comp": L + 2.3, "logo": L + s[1], "sub": L + s[2] + 0.2}
    else:
        L = 0.8; dur = TIM[sid]["dur"] + L + 2.6
        T = {"links": L + s[1], "note": L + s[2]}
    with sync_playwright() as pw:
        p = rec.Page(pw, html=html)
        p.js("([m, T]) => window.__brand.init(m, T)", [sid, T])
        enc = rec.Encoder(out)
        n = int(round(dur * rec.FPS))
        for f in range(n):
            p.js("t => window.__brand.render(t)", f / rec.FPS)
            p.frame(enc, {})
        n = enc.close(); p.close()
    return {"frames": n, "clicks": [], "lead": L}


# ---------------- FAQ and phone ----------------
def more(sid, out):
    html = (HERE / "scenes" / "more.html").read_text(encoding="utf-8")
    dur = dur_of(sid, 1.6)
    with sync_playwright() as pw:
        p = rec.Page(pw, html=html, fake_clock=False)
        p.pg.wait_for_timeout(2500)
        fr = {f.url: f for f in p.pg.frames}
        faq = next(f for u, f in fr.items() if u.endswith("/faq"))
        ph = next(f for u, f in fr.items() if u.rstrip("/").endswith("vercel.app"))
        for f in (faq, ph):
            f.add_style_tag(content="html{scroll-behavior:auto!important}")
        hmax = ph.evaluate("document.documentElement.scrollHeight - innerHeight")
        fmax = faq.evaluate("() => { const d = document.querySelector('details.qa'); return d.getBoundingClientRect().top + scrollY - 120 }")
        enc = rec.Encoder(out)
        n = int(round(dur * rec.FPS)); opened = False
        for f in range(n):
            tt = f / rec.FPS
            ph.evaluate("y => scrollTo(0, y)", io((tt - 0.6) / (dur - 1.2)) * min(hmax, 5200))
            faq.evaluate("y => scrollTo(0, y)", io((tt - 0.4) / 1.4) * fmax)
            if tt > 2.2 and not opened:
                faq.evaluate("() => document.querySelector('details.qa summary').click()"); opened = True
            p.js("t => window.__more && window.__more(t)", tt)
            p.frame(enc, {"chip": chip_state(tt, "07", "FAQ and mobile", dur)})
        n = enc.close(); p.close()
    return {"frames": n, "clicks": [2.2]}


def main():
    meta_p = BUILD / "scenes.json"
    meta = json.loads(meta_p.read_text()) if meta_p.exists() else {}
    for sid in sys.argv[1:]:
        out = BUILD / "scenes" / f"{sid}.mp4"
        fn = brand if sid in ("intro", "outro") else qnx if sid in ("boot", "attack", "shock", "watchdog") \
            else cards if sid in ("instant", "workflow", "proof") else console if sid == "console" \
            else more if sid == "more" else tour
        r = fn(sid, out)
        print(sid, r, flush=True)
        meta = json.loads(meta_p.read_text()) if meta_p.exists() else {}
        meta[sid] = r
        meta_p.write_text(json.dumps(meta, indent=1))


if __name__ == "__main__":
    main()
