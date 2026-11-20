"""Deterministic frame recorder: a headless Chromium page whose clock, CSS
animations and scrolling are advanced one frame at a time, so motion is
perfectly smooth no matter how long each screenshot takes."""
import json, pathlib, subprocess
import imageio_ffmpeg
from playwright.sync_api import sync_playwright

FPS = 30
W, H = 1920, 1080
FF = imageio_ffmpeg.get_ffmpeg_exe()
HERE = pathlib.Path(__file__).parent
OVERLAY_JS = (HERE / "overlay.js").read_text(encoding="utf-8")

STEP_JS = """(dt) => {
  for (const a of document.getAnimations()) {
    if (!a.__ctl) { a.__ctl = true; a.pause(); a.__t = a.currentTime || 0; }
    a.__t += dt; a.currentTime = a.__t;
  }
}"""


class Encoder:
    def __init__(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.p = subprocess.Popen(
            [FF, "-y", "-loglevel", "error", "-f", "image2pipe", "-framerate", str(FPS),
             "-c:v", "mjpeg", "-i", "-", "-c:v", "libx264", "-preset", "slow", "-crf", "15",
             "-pix_fmt", "yuv420p", "-r", str(FPS), str(path)], stdin=subprocess.PIPE)
        self.n = 0

    def add(self, jpg):
        self.p.stdin.write(jpg); self.n += 1

    def close(self):
        self.p.stdin.close(); self.p.wait()
        return self.n


class Page:
    def __init__(self, pw, url=None, html=None, w=W, h=H, mobile=False, fake_clock=True):
        self.b = pw.chromium.launch(args=["--hide-scrollbars", "--force-color-profile=srgb"])
        opts = dict(viewport={"width": w, "height": h}, device_scale_factor=1)
        if mobile:
            opts.update(device_scale_factor=2, is_mobile=True, has_touch=True)
        self.ctx = self.b.new_context(**opts)
        self.pg = self.ctx.new_page()
        self.fake = fake_clock
        if fake_clock:
            self.pg.clock.install()
        if url:
            self.pg.goto(url, wait_until="networkidle")
        else:
            self.pg.set_content(html, wait_until="networkidle")
        self.pg.add_style_tag(content="html{scroll-behavior:auto!important}")
        self.pg.evaluate("document.fonts.ready")
        self.pg.evaluate(OVERLAY_JS)
        self.t = 0.0

    def js(self, code, arg=None):
        return self.pg.evaluate(code, arg)

    def frame(self, enc, state=None, scroll=None):
        dt = 1000.0 / FPS
        if scroll is not None:
            self.pg.evaluate("y => window.scrollTo(0, y)", scroll)
        if self.fake:
            self.pg.clock.run_for(int(round(dt)))
        self.pg.evaluate(STEP_JS, dt)
        self.pg.evaluate("s => window.__ov && window.__ov.render(s)", state or {})
        enc.add(self.pg.screenshot(type="jpeg", quality=93))
        self.t += dt / 1000.0

    def close(self):
        self.b.close()
