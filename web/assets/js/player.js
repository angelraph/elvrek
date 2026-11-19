/* Elvrek recorded run player. Plays back a trace written by the real
   controller code (tests/host/trace_export.c). Nothing here is simulated in
   the browser; the page only draws what the controller recorded. */
(() => {
  "use strict";
  const box = document.querySelector("[data-player]");
  if (!box) return;
  const cv = box.querySelector("canvas"), ctx = cv.getContext("2d");
  const btn = box.querySelector(".play"), range = box.querySelector("input[type=range]");
  const speedSel = box.querySelector("select"), timeEl = box.querySelector(".time");
  const R = k => box.querySelector(`[data-r="${k}"]`);
  const PLAY = '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M7 4l13 8-13 8z"/></svg>';
  const PAUSE = '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M6 4h4v16H6zM14 4h4v16h-4z"/></svg>';
  let data, pos = 0, playing = false, last = 0, w, h;

  const fit = () => { const d = Math.min(devicePixelRatio || 1, 2); w = cv.clientWidth; h = cv.clientHeight; cv.width = w * d; cv.height = h * d; ctx.setTransform(d, 0, 0, d, 0, 0); draw(); };

  const stateName = i => data.states[i].replace(/_/g, " ");
  const handsOff = s => s !== "COMPRESS";

  function draw() {
    if (!data) return;
    const rows = data.rows, n = rows.length, win = 1200;
    const head = Math.min(n - 1, Math.floor(pos));
    const from = Math.max(0, head - win + 1);
    ctx.clearRect(0, 0, w, h);
    const lanes = [
      { k: 0, lo: 0, hi: 62, label: "Chest depth, mm", color: "#e3122d", y0: 0.04, y1: 0.36, guides: [50, 60] },
      { k: 1, lo: 0, hi: 650, label: "Force on the cup, N", color: "#f2b62b", y0: 0.42, y1: 0.66, guides: [600] },
      { k: 2, lo: -1.6, hi: 3.6, label: "ECG, mV (with compression artifact)", color: "#d7dce3", y0: 0.72, y1: 0.96 },
    ];
    // state shading
    for (let i = from; i <= head; i++) {
      const r = rows[i], s = data.states[r[3]], x = (i - from) / (win - 1) * w;
      if (s === "SHOCK_WINDOW") { ctx.fillStyle = "rgba(242,182,43,0.10)"; ctx.fillRect(x, 0, w / win + 1, h); }
      if (s === "ROSC_HOLD") { ctx.fillStyle = "rgba(94,224,138,0.06)"; ctx.fillRect(x, 0, w / win + 1, h); }
      if (r[4]) { ctx.fillStyle = "rgba(242,182,43,0.95)"; ctx.fillRect(x - 1, 0, 3, h); }
      if (r[5]) { ctx.fillStyle = "rgba(90,160,255,0.35)"; ctx.fillRect(x, h * 0.985, w / win + 1, 3); }
    }
    lanes.forEach(L => {
      const y0 = L.y0 * h, y1 = L.y1 * h;
      ctx.fillStyle = "#6f7780"; ctx.font = "12px Inter, sans-serif"; ctx.fillText(L.label, 10, y0 + 14);
      (L.guides || []).forEach(g => {
        const y = y1 - (g - L.lo) / (L.hi - L.lo) * (y1 - y0);
        ctx.strokeStyle = "rgba(255,255,255,0.12)"; ctx.setLineDash([4, 6]); ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke(); ctx.setLineDash([]);
        ctx.fillStyle = "#59606a"; ctx.fillText(g, w - 34, y - 4);
      });
      ctx.strokeStyle = L.color; ctx.lineWidth = 1.7; ctx.beginPath();
      for (let i = from; i <= head; i++) {
        const v = (rows[i][L.k] - L.lo) / (L.hi - L.lo);
        const x = (i - from) / (win - 1) * w, y = y1 - Math.max(0, Math.min(1, v)) * (y1 - y0);
        i === from ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      }
      ctx.stroke();
    });
    const r = rows[head], s = data.states[r[3]];
    R("state").textContent = stateName(r[3]);
    R("state").classList.toggle("red", handsOff(s));
    R("depth").textContent = r[0].toFixed(1) + " mm";
    R("force").textContent = r[1] + " N";
    R("etco2").textContent = r[6].toFixed(0) + " mmHg";
    R("shock").textContent = rows.slice(0, head + 1).some(x => x[4]) ? "Delivered" : "Not yet";
    const sec = data.start_s + head / data.hz;
    timeEl.textContent = sec.toFixed(1) + " s";
    range.value = head;
  }

  const loop = now => {
    requestAnimationFrame(loop);
    if (!playing || !data) { last = now; return; }
    const dt = (now - last) / 1000; last = now;
    pos += dt * data.hz * parseFloat(speedSel.value);
    if (pos >= data.rows.length - 1) { pos = data.rows.length - 1; playing = false; btn.innerHTML = PLAY; }
    draw();
  };

  btn.addEventListener("click", () => {
    if (!data) return;
    if (pos >= data.rows.length - 1) pos = 0;
    playing = !playing; btn.innerHTML = playing ? PAUSE : PLAY;
  });
  range.addEventListener("input", () => { pos = +range.value; draw(); });
  addEventListener("resize", fit);

  (window.ElvrekTrace ? window.ElvrekTrace() : fetch("/data/run-vf-shock.json").then(r => r.json())).then(d => {
    data = d;
    range.max = d.rows.length - 1;
    pos = 1100;
    const S = d.summary;
    const set = (k, v) => { const el = box.parentElement.querySelector(`[data-s="${k}"]`); if (el) el.textContent = v; };
    set("machine", (S.machine_ticks / 1000).toFixed(2) + " s");
    set("human", (S.human_ticks / 1000).toFixed(2) + " s");
    set("ccf", (S.ccf * 100).toFixed(1) + "%");
    set("viol", S.invariant_violations);
    fit();
    btn.innerHTML = PLAY;
    if (box.dataset.autoplay !== undefined) { playing = true; btn.innerHTML = PAUSE; }
  });
  document.addEventListener("modal-open", () => { setTimeout(fit, 50); playing = true; btn.innerHTML = PAUSE; });
  document.addEventListener("modal-close", () => { playing = false; btn.innerHTML = PLAY; });
  requestAnimationFrame(loop);
})();
