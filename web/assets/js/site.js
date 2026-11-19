/* Elvrek website behaviour. Plain JavaScript, no framework, no tracking. */
(() => {
  "use strict";

  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => [...r.querySelectorAll(s)];
  const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const page = document.body.dataset.page || "";
  const REPO = "https://github.com/angelraph/elvrek";

  const ICON = {
    arrow: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12h14M13 6l6 6-6 6"/></svg>',
    down: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 8l8 8 8-8"/></svg>',
    up: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"><path d="M5 15l7-7 7 7"/></svg>',
    left: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"><path d="M15 5l-7 7 7 7"/></svg>',
    right: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"><path d="M9 5l7 7-7 7"/></svg>',
    play: '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M7 4l13 8-13 8z"/></svg>',
    pause: '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M6 4h4v16H6zM14 4h4v16h-4z"/></svg>',
    playpause: '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M3 5l9 7-9 7zM14 5h3v14h-3zM19 5h3v14h-3z"/></svg>',
    zoom: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5L21 21M10.5 7.5v6M7.5 10.5h6"/></svg>',
    full: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/></svg>',
    github: '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 .5a11.5 11.5 0 00-3.6 22.4c.6.1.8-.3.8-.6v-2c-3.2.7-3.9-1.5-3.9-1.5-.5-1.3-1.3-1.7-1.3-1.7-1-.7.1-.7.1-.7 1.2.1 1.8 1.2 1.8 1.2 1 1.8 2.8 1.3 3.5 1 .1-.8.4-1.3.7-1.6-2.6-.3-5.3-1.3-5.3-5.7 0-1.3.5-2.3 1.2-3.1-.1-.3-.5-1.5.1-3.1 0 0 1-.3 3.2 1.2a11 11 0 015.8 0c2.2-1.5 3.2-1.2 3.2-1.2.6 1.6.2 2.8.1 3.1.8.8 1.2 1.8 1.2 3.1 0 4.4-2.7 5.4-5.3 5.7.4.4.8 1.1.8 2.2v3.3c0 .3.2.7.8.6A11.5 11.5 0 0012 .5z"/></svg>',
    pulse: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M2 12h5l2-6 4 12 2-6h7"/></svg>',
    chat: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 5h16v11H8l-4 4z"/></svg>',
    book: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 4h7a3 3 0 013 3v13a2 2 0 00-2-2H4zM20 4h-6M20 4v14h-6"/></svg>',
  };

  /* Header, side rail, footer: one source for every page */
  const header = `
  <header class="site-header" id="top">
    <div class="wrap">
      <a class="brand" href="/" aria-label="Elvrek home"><img src="/assets/elvrek-logo.webp" alt=""><span><b>E</b>LVREK</span></a>
      <button class="burger" aria-label="Open menu" aria-expanded="false"><span></span><span></span><span></span></button>
      <nav class="nav" aria-label="Main">
        <div class="drop" data-group="system technology evidence simulator">
          <button><span class="plus">+</span>Product</button>
          <div class="drop-menu">
            <a href="/system"><strong>The system</strong><small>Piston, breaths and shock on one clock</small></a>
            <a href="/technology"><strong>Technology</strong><small>How it runs on QNX 8.0</small></a>
            <a href="/evidence"><strong>Evidence</strong><small>What we tested and what we found</small></a>
            <a href="/simulator"><strong>Recorded run</strong><small>Watch the real controller work</small></a>
          </div>
        </div>
        <a href="/about" data-p="about">About</a>
        <a href="/faq" data-p="faq">FAQ</a>
        <a href="/contact" data-p="contact">Contact</a>
        <a class="btn btn-red nav-cta" href="/simulator">Watch it run</a>
      </nav>
    </div>
  </header>
  <div class="rail" aria-label="Quick links">
    <a href="${REPO}" target="_blank" rel="noopener">${ICON.github}<span>Source code</span></a>
    <a href="/simulator">${ICON.pulse}<span>Recorded run</span></a>
    <a href="/faq">${ICON.book}<span>Questions</span></a>
    <a href="/contact">${ICON.chat}<span>Get in touch</span></a>
  </div>
  <button class="to-top" aria-label="Back to top">${ICON.up}</button>`;

  const footer = `
  <footer class="site-footer">
    <div class="wrap">
      <div class="foot-grid">
        <div class="foot-brand">
          <img src="/assets/elvrek-logo.webp" alt="Elvrek">
          <p>A real-time safety controller for mechanical CPR, built on QNX 8.0. Research software, not a medical device.</p>
        </div>
        <div><h5>Product</h5><a href="/system">The system</a><a href="/technology">Technology</a><a href="/evidence">Evidence</a><a href="/simulator">Recorded run</a></div>
        <div><h5>Learn</h5><a href="/faq">FAQ</a><a href="/technology#states">State machine</a><a href="/evidence#ecg">Real ECG results</a><a href="${REPO}" target="_blank" rel="noopener">Source code</a></div>
        <div><h5>Company</h5><a href="/about">About</a><a href="/about#roadmap">Roadmap</a><a href="/contact">Contact</a></div>
        <div><h5>Legal</h5><a href="/safety">Safety notice</a><a href="/privacy">Privacy</a><a href="/terms">Terms of use</a></div>
      </div>
      <div class="foot-base">
        <span>&copy; ${new Date().getFullYear()} Elvrek. Code under the Apache License 2.0.</span>
        <span>Not a medical device. Never connect to a person.</span>
      </div>
    </div>
  </footer>`;

  const hh = $("[data-header]");
  if (hh) hh.outerHTML = header;
  const ff = $("[data-footer]");
  if (ff) ff.outerHTML = footer;

  // active nav state
  $$(".nav [data-p]").forEach(a => a.classList.toggle("active", a.dataset.p === page));
  $$(".nav .drop").forEach(d => d.classList.toggle("active", d.dataset.group.split(" ").includes(page)));

  // header solid on scroll, back to top
  const head = $(".site-header");
  const top = $(".to-top");
  const onScroll = () => {
    const y = scrollY;
    head && head.classList.toggle("solid", y > 40 || !$(".hero"));
    top && top.classList.toggle("show", y > 700);
  };
  addEventListener("scroll", onScroll, { passive: true });
  onScroll();
  top && top.addEventListener("click", () => scrollTo({ top: 0, behavior: reduce ? "auto" : "smooth" }));

  // mobile menu
  const burger = $(".burger"), nav = $(".nav");
  burger && burger.addEventListener("click", () => {
    const o = nav.classList.toggle("open");
    burger.setAttribute("aria-expanded", o);
  });
  $$(".drop > button").forEach(b => b.addEventListener("click", () => b.parentElement.classList.toggle("open")));
  $$(".nav a").forEach(a => a.addEventListener("click", () => { nav.classList.remove("open"); burger && burger.setAttribute("aria-expanded", "false"); }));

  // thin progress line when leaving the page
  const line = document.createElement("div");
  line.className = "pageline";
  document.body.appendChild(line);
  $$("a[href^='/']").forEach(a => a.addEventListener("click", e => {
    if (e.metaKey || e.ctrlKey || a.target) return;
    line.style.width = "70%";
  }));
  addEventListener("pageshow", () => { line.style.width = "0"; });

  /* Reveal on scroll */
  const io = new IntersectionObserver(es => es.forEach(e => {
    if (e.isIntersecting) { e.target.classList.add("in"); io.unobserve(e.target); }
  }), { threshold: 0.14, rootMargin: "0px 0px -40px 0px" });
  $$(".stagger").forEach(g => [...g.children].forEach((c, i) => { c.style.setProperty("--i", i); c.classList.add("reveal"); }));
  $$(".reveal, .reveal-left, .reveal-right").forEach(el => io.observe(el));

  /* Count up numbers */
  const cio = new IntersectionObserver(es => es.forEach(e => {
    if (!e.isIntersecting) return;
    cio.unobserve(e.target);
    const el = e.target, to = parseFloat(el.dataset.count), dec = (el.dataset.count.split(".")[1] || "").length;
    const suffix = el.querySelector("small") ? el.querySelector("small").outerHTML : "";
    const t0 = performance.now(), dur = reduce ? 0 : 1600;
    const step = t => {
      const p = dur ? Math.min(1, (t - t0) / dur) : 1;
      const v = to * (1 - Math.pow(1 - p, 4));
      el.innerHTML = v.toFixed(dec) + suffix;
      if (p < 1) requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  }), { threshold: 0.5 });
  $$("[data-count]").forEach(el => cio.observe(el));

  /* Mouse light on cards */
  $$(".card").forEach(c => c.addEventListener("pointermove", e => {
    const r = c.getBoundingClientRect();
    c.style.setProperty("--mx", (e.clientX - r.left) + "px");
    c.style.setProperty("--my", (e.clientY - r.top) + "px");
  }));

  /* Parallax */
  const px = $$(".parallax");
  if (px.length && !reduce) {
    const run = () => {
      px.forEach(el => {
        const r = el.getBoundingClientRect();
        const d = (r.top + r.height / 2 - innerHeight / 2) * (parseFloat(el.dataset.speed) || 0.12);
        el.style.transform = `translate3d(0, ${-d}px, 0)`;
      });
    };
    addEventListener("scroll", () => requestAnimationFrame(run), { passive: true });
    run();
  }

  /* Sticky section bar with scrollspy and sliding ink */
  $$(".subnav").forEach(bar => {
    const links = $$("a", bar), wrap = $(".wrap", bar);
    const ink = document.createElement("span");
    ink.className = "ink";
    wrap.appendChild(ink);
    const sections = links.map(a => $(a.getAttribute("href"))).filter(Boolean);
    const set = a => {
      links.forEach(l => l.classList.toggle("on", l === a));
      ink.style.left = a.offsetLeft + "px";
      ink.style.width = a.offsetWidth + "px";
      if (wrap.scrollWidth > wrap.clientWidth) wrap.scrollTo({ left: a.offsetLeft - 40, behavior: "smooth" });
    };
    const spy = () => {
      let cur = sections[0];
      sections.forEach(s => { if (s.getBoundingClientRect().top < 180) cur = s; });
      const a = links.find(l => l.getAttribute("href") === "#" + cur.id);
      if (a && !a.classList.contains("on")) set(a);
    };
    addEventListener("scroll", () => requestAnimationFrame(spy), { passive: true });
    addEventListener("resize", () => { const on = $("a.on", bar); on && set(on); });
    set(links[0]);
    spy();
  });

  /* Tabs */
  $$("[data-tabs]").forEach(box => {
    const btns = $$(".tabs button", box), panels = $$(".tab-panel", box);
    const show = i => { btns.forEach((b, j) => b.classList.toggle("on", i === j)); panels.forEach((p, j) => p.classList.toggle("on", i === j)); };
    btns.forEach((b, i) => b.addEventListener("click", () => show(i)));
    const want = location.hash && panels.findIndex(p => "#" + p.id === location.hash);
    show(want > 0 ? want : 0);
  });

  /* Split panel slider with dash indicators */
  $$("[data-slider]").forEach(box => {
    const slides = $$(".slide", box), dots = $(".dashes", box);
    let i = 0, timer;
    slides.forEach((_, j) => { const b = document.createElement("button"); b.setAttribute("aria-label", "Slide " + (j + 1)); b.onclick = () => go(j, true); dots.appendChild(b); });
    const go = (n, user) => {
      i = (n + slides.length) % slides.length;
      slides.forEach((s, j) => s.classList.toggle("on", j === i));
      $$("button", dots).forEach((b, j) => b.classList.toggle("on", j === i));
      if (user) restart();
    };
    const restart = () => { clearInterval(timer); if (!reduce) timer = setInterval(() => go(i + 1), 5200); };
    $(".prev", box).onclick = () => go(i - 1, true);
    $(".next", box).onclick = () => go(i + 1, true);
    go(0); restart();
  });

  /* Paged carousel with 1 / n counter */
  $$("[data-pager]").forEach(box => {
    const track = $(".pager-track", box), items = [...track.children];
    const prev = $(".pager-controls .prev", box), next = $(".pager-controls .next", box), count = $(".pager-controls .count", box);
    let i = 0;
    const per = () => innerWidth < 640 ? 1 : innerWidth < 1000 ? 2 : 3;
    const pages = () => Math.max(1, items.length - per() + 1);
    const go = n => {
      i = Math.max(0, Math.min(n, pages() - 1));
      const w = items[0].getBoundingClientRect().width + 22;
      track.style.transform = `translate3d(${-i * w}px,0,0)`;
      count.textContent = `${i + 1} / ${pages()}`;
      prev.disabled = i === 0; next.disabled = i === pages() - 1;
    };
    prev.onclick = () => go(i - 1);
    next.onclick = () => go(i + 1);
    addEventListener("resize", () => go(i));
    let sx = null;
    track.addEventListener("pointerdown", e => { sx = e.clientX; });
    addEventListener("pointerup", e => { if (sx === null) return; const d = e.clientX - sx; sx = null; if (Math.abs(d) > 50) go(i + (d < 0 ? 1 : -1)); });
    go(0);
  });

  /* Modal */
  $$("[data-modal]").forEach(btn => btn.addEventListener("click", e => {
    e.preventDefault();
    const m = $(btn.dataset.modal);
    m.classList.add("open");
    document.dispatchEvent(new CustomEvent("modal-open", { detail: m }));
  }));
  $$(".modal").forEach(m => {
    const close = () => { m.classList.remove("open"); document.dispatchEvent(new CustomEvent("modal-close", { detail: m })); };
    m.addEventListener("click", e => { if (e.target === m) close(); });
    $(".modal-close", m) && $(".modal-close", m).addEventListener("click", close);
    addEventListener("keydown", e => { if (e.key === "Escape" && m.classList.contains("open")) close(); });
  });

  /* Hero canvas: a cinematic loop of the arm compressing a chest, a monitor
     trace and a slow light sweep, drawn live instead of a heavy video file. */
  $$("canvas.hero-bg").forEach(cv => {
    const ctx = cv.getContext("2d");
    const big = cv.dataset.mode !== "soft";
    let w, h, dpr, t0 = performance.now(), visible = true;
    const fit = () => {
      dpr = Math.min(devicePixelRatio || 1, 2);
      w = cv.clientWidth; h = cv.clientHeight;
      cv.width = w * dpr; cv.height = h * dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    };
    fit();
    addEventListener("resize", fit);
    new IntersectionObserver(es => { visible = es[0].isIntersecting; }).observe(cv);
    const dust = Array.from({ length: 70 }, () => ({ x: Math.random(), y: Math.random(), r: Math.random() * 1.6 + 0.3, s: Math.random() * 0.02 + 0.004 }));

    const ecg = p => {
      p = p % 1;
      if (p < 0.08) return Math.sin(p / 0.08 * Math.PI) * 0.08;
      if (p < 0.14) return 0;
      if (p < 0.16) return -0.15;
      if (p < 0.19) return 1;
      if (p < 0.22) return -0.35;
      if (p < 0.34) return 0;
      if (p < 0.48) return Math.sin((p - 0.34) / 0.14 * Math.PI) * 0.22;
      return 0;
    };

    const draw = now => {
      requestAnimationFrame(draw);
      if (!visible) return;
      const t = (now - t0) / 1000;
      ctx.clearRect(0, 0, w, h);
      // base
      const g = ctx.createRadialGradient(w * 0.55, h * 0.45, 0, w * 0.55, h * 0.45, Math.max(w, h) * 0.8);
      g.addColorStop(0, "#1a1d24"); g.addColorStop(1, "#050607");
      ctx.fillStyle = g; ctx.fillRect(0, 0, w, h);
      // grid
      ctx.strokeStyle = "rgba(255,255,255,0.035)"; ctx.lineWidth = 1;
      const gs = 48, off = (t * 12) % gs;
      for (let x = -off; x < w; x += gs) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke(); }
      for (let y = 0; y < h; y += gs) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke(); }
      // light sweep
      const sx = ((t * 0.08) % 1.6 - 0.3) * w;
      const sg = ctx.createLinearGradient(sx - 300, 0, sx + 300, 0);
      sg.addColorStop(0, "rgba(227,18,45,0)"); sg.addColorStop(0.5, "rgba(227,18,45,0.10)"); sg.addColorStop(1, "rgba(227,18,45,0)");
      ctx.fillStyle = sg; ctx.fillRect(0, 0, w, h);

      if (big) {
        // compression cycle at 110 per minute
        const cyc = (t * 110 / 60) % 1;
        const depth = 0.5 * (1 - Math.cos(2 * Math.PI * cyc));
        const wide = w > 980;
        const s = wide ? Math.min(w / 2150, h / 900) : w / 1100;
        const cx = wide ? w * 0.79 : w * 0.8, base = h * (wide ? 0.84 : 0.95);
        ctx.globalAlpha = wide ? 1 : 0.5;
        // the patient, lying on their back, head to the left
        ctx.save();
        ctx.translate(cx, base);
        drawPatient(ctx, s, depth * 22 * s, w, 1);
        // arm frame
        const col = ctx.createLinearGradient(-60 * s, 0, 60 * s, 0);
        col.addColorStop(0, "#8d939b"); col.addColorStop(0.5, "#e6e9ee"); col.addColorStop(1, "#6d727a");
        ctx.fillStyle = col;
        const top = -470 * s;
        roundRect(ctx, -250 * s, top, 500 * s, 70 * s, 18 * s); ctx.fill();
        roundRect(ctx, -250 * s, top, 54 * s, 470 * s, 16 * s); ctx.fill();
        roundRect(ctx, 196 * s, top, 54 * s, 470 * s, 16 * s); ctx.fill();
        // red accents
        ctx.fillStyle = "#e3122d";
        roundRect(ctx, -238 * s, top + 120 * s, 30 * s, 90 * s, 8 * s); ctx.fill();
        roundRect(ctx, 208 * s, top + 120 * s, 30 * s, 90 * s, 8 * s); ctx.fill();
        // piston column
        const pz = top + 70 * s + depth * 26 * s;
        ctx.fillStyle = "#1c1f25";
        roundRect(ctx, -46 * s, top + 40 * s, 92 * s, 150 * s, 14 * s); ctx.fill();
        ctx.fillStyle = col;
        roundRect(ctx, -18 * s, pz + 100 * s, 36 * s, 190 * s, 8 * s); ctx.fill();
        // cup
        ctx.fillStyle = "#e3122d";
        ctx.beginPath(); ctx.ellipse(0, pz + 296 * s, 62 * s, 16 * s, 0, 0, Math.PI * 2); ctx.fill();
        ctx.shadowColor = "#e3122d"; ctx.shadowBlur = 30 * s * (0.4 + depth);
        ctx.fillRect(-40 * s, pz + 288 * s, 80 * s, 6 * s);
        ctx.shadowBlur = 0;
        // display glow on the column
        ctx.fillStyle = `rgba(227,18,45,${0.5 + 0.5 * depth})`;
        roundRect(ctx, -26 * s, top + 70 * s, 52 * s, 30 * s, 6 * s); ctx.fill();
        ctx.restore();
        ctx.globalAlpha = 1;
      } else {
        // inner pages: the same patient, faint, along the bottom of the header
        const s = Math.min(w / 2400, h / 620);
        ctx.save();
        ctx.globalAlpha = 0.9;
        ctx.translate(w * 0.8, h * 0.9);
        const cyc = (t * 110 / 60) % 1;
        drawPatient(ctx, s, 0.5 * (1 - Math.cos(2 * Math.PI * cyc)) * 10 * s, w, 0.6);
        ctx.restore();
        ctx.globalAlpha = 1;
      }

      // monitor trace
      const ty = big ? h * 0.2 : h * 0.62, amp = big ? 34 : 46;
      ctx.lineWidth = 2; ctx.strokeStyle = "rgba(227,18,45,0.85)"; ctx.shadowColor = "#e3122d"; ctx.shadowBlur = 10;
      ctx.beginPath();
      const speed = 180, head = (t * speed) % (w + 200);
      for (let x = Math.max(0, head - w * 0.55); x <= head && x <= w; x += 2) {
        const v = ecg((x - t * 0) / 220);
        const y = ty - v * amp;
        x === Math.max(0, head - w * 0.55) ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      }
      ctx.stroke(); ctx.shadowBlur = 0;
      ctx.fillStyle = "#fff"; ctx.beginPath(); ctx.arc(Math.min(head, w), ty - ecg(Math.min(head, w) / 220) * amp, 3, 0, Math.PI * 2); ctx.fill();

      // dust
      ctx.fillStyle = "rgba(255,255,255,0.35)";
      dust.forEach(d => {
        d.y -= d.s * 0.02; if (d.y < 0) d.y = 1;
        ctx.globalAlpha = 0.25 + 0.25 * Math.sin(t + d.x * 10);
        ctx.beginPath(); ctx.arc(d.x * w, d.y * h, d.r, 0, Math.PI * 2); ctx.fill();
      });
      ctx.globalAlpha = 1;
    };
    requestAnimationFrame(draw);
  });

  /* A person lying on their back, seen from the side, head to the left.
     Origin is the sternum at table level; dip pushes the chest down under
     the cup. Drawn as a soft grey render with a light rim, like a studio shot. */
  function drawPatient(c, s, dip, w, light) {
    const P = (x, y) => [x * s, y * s];
    c.save();
    const g = c.createLinearGradient(0, -110 * s, 0, 70 * s);
    g.addColorStop(0, `rgba(122,130,142,${light})`);
    g.addColorStop(0.35, `rgba(62,67,76,${light})`);
    g.addColorStop(1, "rgba(10,11,14,1)");
    c.fillStyle = g;
    c.beginPath();
    c.moveTo(-w * 2, 70 * s);
    c.lineTo(...P(-660, 70));
    c.bezierCurveTo(...P(-672, 10), ...P(-640, -52), ...P(-575, -74));   // back of the skull
    c.bezierCurveTo(...P(-548, -84), ...P(-520, -86), ...P(-505, -84));  // forehead
    c.bezierCurveTo(...P(-496, -86), ...P(-490, -96), ...P(-482, -101)); // bridge of the nose
    c.bezierCurveTo(...P(-476, -98), ...P(-472, -88), ...P(-470, -84));  // tip of the nose
    c.bezierCurveTo(...P(-466, -84), ...P(-462, -80), ...P(-460, -76));  // lips
    c.bezierCurveTo(...P(-456, -74), ...P(-450, -73), ...P(-446, -70));  // chin
    c.bezierCurveTo(...P(-438, -58), ...P(-424, -46), ...P(-404, -42));  // under the jaw
    c.bezierCurveTo(...P(-380, -40), ...P(-350, -50), ...P(-318, -62));  // neck to collarbone
    c.bezierCurveTo(...P(-250, -84), ...P(-150, -96), ...P(-60, -99));   // upper chest
    c.quadraticCurveTo(0, -100 * s + dip * 1.5, 60 * s, -98 * s);        // sternum under the cup
    c.bezierCurveTo(...P(150, -96), ...P(230, -88), ...P(300, -78));     // lower ribs
    c.bezierCurveTo(...P(360, -70), ...P(420, -76), ...P(480, -74));     // belly
    c.bezierCurveTo(...P(560, -72), ...P(640, -66), w * 2, -64 * s);     // hips and legs
    c.lineTo(w * 2, 70 * s);
    c.closePath();
    c.fill();
    // rim light along the top of the body
    c.strokeStyle = `rgba(235,240,246,${0.55 * light})`;
    c.lineWidth = 1.6;
    c.shadowColor = "rgba(235,240,246,0.35)";
    c.shadowBlur = 8;
    c.stroke();
    c.shadowBlur = 0;
    // ear and a closed eye
    c.fillStyle = "rgba(20,22,27,0.55)";
    c.beginPath(); c.ellipse(-590 * s, -30 * s, 14 * s, 20 * s, -0.3, 0, Math.PI * 2); c.fill();
    c.strokeStyle = `rgba(20,22,27,${0.7 * light})`; c.lineWidth = 2 * s;
    c.beginPath(); c.arc(-522 * s, -74 * s, 9 * s, 0.2, Math.PI - 0.2); c.stroke();
    // table under the patient
    c.fillStyle = "#08090b";
    c.fillRect(-w * 2, 70 * s, w * 4, 400 * s);
    c.strokeStyle = "rgba(227,18,45,0.35)"; c.lineWidth = 1;
    c.beginPath(); c.moveTo(-w * 2, 70 * s); c.lineTo(w * 2, 70 * s); c.stroke();
    c.restore();
  }

  function roundRect(c, x, y, w, h, r) {
    c.beginPath();
    c.moveTo(x + r, y);
    c.arcTo(x + w, y, x + w, y + h, r);
    c.arcTo(x + w, y + h, x, y + h, r);
    c.arcTo(x, y + h, x, y, r);
    c.arcTo(x, y, x + w, y, r);
    c.closePath();
  }

  /* 360 degree viewer: a small software renderer for the arm, with drag to
     rotate, play, zoom and fullscreen controls. */
  $$("[data-viewer]").forEach(box => {
    const cv = $("canvas", box), ctx = cv.getContext("2d");
    let yaw = -0.6, pitch = 0.32, zoom = 1, playing = !reduce, drag = null, w, h, dpr, t0 = performance.now();
    const fit = () => {
      dpr = Math.min(devicePixelRatio || 1, 2);
      w = cv.clientWidth; h = cv.clientHeight;
      cv.width = w * dpr; cv.height = h * dpr; ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    };
    fit(); addEventListener("resize", fit);

    const boxF = (x0, y0, z0, x1, y1, z1, color) => {
      const v = [[x0,y0,z0],[x1,y0,z0],[x1,y1,z0],[x0,y1,z0],[x0,y0,z1],[x1,y0,z1],[x1,y1,z1],[x0,y1,z1]];
      return [[0,1,2,3],[5,4,7,6],[4,0,3,7],[1,5,6,2],[3,2,6,7],[4,5,1,0]].map(f => ({ p: f.map(i => v[i]), color }));
    };
    const cyl = (cx, y0, y1, cz, r, n, color, capColor) => {
      const out = [];
      for (let i = 0; i < n; i++) {
        const a0 = i / n * Math.PI * 2, a1 = (i + 1) / n * Math.PI * 2;
        const p0 = [cx + Math.cos(a0) * r, y0, cz + Math.sin(a0) * r], p1 = [cx + Math.cos(a1) * r, y0, cz + Math.sin(a1) * r];
        out.push({ p: [p0, p1, [p1[0], y1, p1[2]], [p0[0], y1, p0[2]]], color });
      }
      const cap = []; for (let i = 0; i < n; i++) { const a = i / n * Math.PI * 2; cap.push([cx + Math.cos(a) * r, y1, cz + Math.sin(a) * r]); }
      out.push({ p: cap.reverse(), color: capColor || color });
      return out;
    };
    const model = depth => [
      ...boxF(-1.7, 1.55, -0.55, 1.7, 1.7, 0.55, "#2a2e35"),            // back board
      ...boxF(-1.55, -1.45, -0.28, -1.15, 1.55, 0.28, "chrome"),       // left leg
      ...boxF(1.15, -1.45, -0.28, 1.55, 1.55, 0.28, "chrome"),         // right leg
      ...boxF(-1.55, -1.8, -0.32, 1.55, -1.3, 0.32, "chrome"),         // top bar
      ...boxF(-1.5, -0.6, -0.3, -1.2, 0.0, 0.3, "red"),                // left handle accent
      ...boxF(1.2, -0.6, -0.3, 1.5, 0.0, 0.3, "red"),                  // right handle accent
      ...boxF(-0.42, -1.75, -0.42, 0.42, -0.85, 0.42, "#1b1e24"),      // drive housing
      ...boxF(-0.3, -1.5, 0.42, 0.3, -1.25, 0.44, "glow"),             // display
      ...cyl(0, -0.85, 0.35 + depth * 0.35, 0, 0.12, 14, "chrome"),    // piston rod
      ...cyl(0, 0.35 + depth * 0.35, 0.52 + depth * 0.35, 0, 0.42, 22, "red", "red"), // cup
    ];

    const shade = (color, k) => {
      const base = color === "chrome" ? [214, 220, 228] : color === "red" ? [227, 18, 45] : color === "glow" ? [255, 60, 80] : hex(color);
      const m = color === "glow" ? 1 : 0.35 + 0.65 * k;
      return `rgb(${base.map(c => Math.round(c * m)).join(",")})`;
    };
    function hex(c) { const n = parseInt(c.slice(1), 16); return [n >> 16, (n >> 8) & 255, n & 255]; }
    const sub = (a, b) => [a[0]-b[0], a[1]-b[1], a[2]-b[2]];
    const cross = (a, b) => [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]];
    const norm = a => { const l = Math.hypot(...a) || 1; return a.map(x => x / l); };

    const render = now => {
      requestAnimationFrame(render);
      const t = (now - t0) / 1000;
      if (playing && !drag) yaw += 0.006;
      const depth = playing ? 0.5 * (1 - Math.cos(t * 110 / 60 * Math.PI * 2)) : 0;
      ctx.clearRect(0, 0, w, h);
      const fl = ctx.createRadialGradient(w / 2, h * 0.86, 10, w / 2, h * 0.86, w * 0.45);
      fl.addColorStop(0, "rgba(227,18,45,0.18)"); fl.addColorStop(1, "rgba(227,18,45,0)");
      ctx.fillStyle = fl; ctx.fillRect(0, 0, w, h);
      const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
      const tf = p => {
        let x = p[0] * cy - p[2] * sy, z = p[0] * sy + p[2] * cy, y = p[1];
        const y2 = y * cp - z * sp, z2 = y * sp + z * cp;
        return [x, y2, z2 + 7];
      };
      const sc = Math.min(w, h) * 1.3 * zoom;
      const proj = p => [w / 2 + p[0] / p[2] * sc, h * 0.47 + p[1] / p[2] * sc, p[2]];
      const light = norm([-0.4, -0.8, -0.6]);
      const faces = model(depth).map(f => {
        const tp = f.p.map(tf);
        const n = norm(cross(sub(tp[1], tp[0]), sub(tp[2], tp[0])));
        const zc = tp.reduce((a, p) => a + p[2], 0) / tp.length;
        return { tp, n, zc, color: f.color };
      }).filter(f => f.n[2] < 0.05).sort((a, b) => b.zc - a.zc);
      faces.forEach(f => {
        const k = Math.max(0, -(f.n[0] * light[0] + f.n[1] * light[1] + f.n[2] * light[2]));
        const pts = f.tp.map(proj);
        ctx.beginPath(); pts.forEach((p, i) => i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1])); ctx.closePath();
        ctx.fillStyle = shade(f.color, k); ctx.fill();
        ctx.strokeStyle = "rgba(0,0,0,0.25)"; ctx.lineWidth = 0.6; ctx.stroke();
      });
    };
    requestAnimationFrame(render);

    cv.addEventListener("pointerdown", e => { drag = { x: e.clientX, y: e.clientY, yaw, pitch }; cv.setPointerCapture(e.pointerId); });
    cv.addEventListener("pointermove", e => {
      if (!drag) return;
      yaw = drag.yaw + (e.clientX - drag.x) * 0.01;
      pitch = Math.max(-0.2, Math.min(0.9, drag.pitch + (e.clientY - drag.y) * 0.005));
    });
    cv.addEventListener("pointerup", () => { drag = null; });
    const tools = $(".viewer-tools", box);
    tools.innerHTML = `<button data-a="play" aria-label="Play or pause">${ICON.playpause}</button><button data-a="zoom" aria-label="Zoom">${ICON.zoom}</button><button data-a="full" aria-label="Fullscreen">${ICON.full}</button>`;
    tools.addEventListener("click", e => {
      const a = e.target.closest("button"); if (!a) return;
      if (a.dataset.a === "play") playing = !playing;
      if (a.dataset.a === "zoom") zoom = zoom === 1 ? 1.45 : 1;
      if (a.dataset.a === "full") { box.classList.toggle("full"); setTimeout(fit, 30); }
    });
    addEventListener("keydown", e => { if (e.key === "Escape" && box.classList.contains("full")) { box.classList.remove("full"); setTimeout(fit, 30); } });
  });

  /* Small animated thumbnails for scenario cards */
  $$("canvas[data-thumb]").forEach(cv => {
    const ctx = cv.getContext("2d"), kind = cv.dataset.thumb;
    let w, h;
    const fit = () => { w = cv.clientWidth; h = cv.clientHeight; cv.width = w * 2; cv.height = h * 2; ctx.setTransform(2, 0, 0, 2, 0, 0); };
    fit(); addEventListener("resize", fit);
    let vis = false;
    new IntersectionObserver(es => { vis = es[0].isIntersecting; }).observe(cv);
    const f = (x, t) => {
      const c = 0.5 * (1 - Math.cos(2 * Math.PI * (x * 4 + t)));
      if (kind === "shock") return x > 0.45 && x < 0.55 ? 0 : c;
      if (kind === "rosc") return x > 0.6 ? 0 : c;
      if (kind === "fault") return x > 0.5 ? Math.max(0, c * (1 - (x - 0.5) * 6)) : c;
      if (kind === "transport") return c * (0.92 + 0.08 * Math.sin(x * 40 + t * 3));
      if (kind === "stiff") return Math.min(c, 0.72);
      return c;
    };
    const draw = now => {
      requestAnimationFrame(draw);
      if (!vis) return;
      const t = now / 1000 * 0.4;
      ctx.clearRect(0, 0, w, h);
      ctx.strokeStyle = "rgba(255,255,255,0.06)";
      for (let y = h * 0.2; y < h; y += h * 0.2) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke(); }
      ctx.strokeStyle = "#e3122d"; ctx.lineWidth = 2; ctx.shadowColor = "#e3122d"; ctx.shadowBlur = 8;
      ctx.beginPath();
      for (let i = 0; i <= 200; i++) { const x = i / 200, y = h * 0.25 + f(x, t) * h * 0.5; i ? ctx.lineTo(x * w, y) : ctx.moveTo(x * w, y); }
      ctx.stroke(); ctx.shadowBlur = 0;
      if (kind === "shock") { ctx.fillStyle = "rgba(242,182,43,0.9)"; ctx.fillRect(w * 0.5 - 1.5, h * 0.12, 3, h * 0.76); }
    };
    requestAnimationFrame(draw);
  });

  /* Shared trace loader for the live strip and the full player */
  let tracePromise = null;
  const loadTrace = () => tracePromise || (tracePromise = fetch("/data/run-vf-shock.json").then(r => r.json()));
  window.ElvrekTrace = loadTrace;

  /* Live strip on the landing page */
  $$("[data-live]").forEach(box => {
    const cv = $("canvas", box), st = $(".state", box), ctx = cv.getContext("2d");
    let w, h, data, start = performance.now(), vis = false;
    const fit = () => { w = cv.clientWidth; h = cv.clientHeight; cv.width = w * 2; cv.height = h * 2; ctx.setTransform(2, 0, 0, 2, 0, 0); };
    fit(); addEventListener("resize", fit);
    new IntersectionObserver(es => { vis = es[0].isIntersecting; }).observe(cv);
    loadTrace().then(d => { data = d; });
    const draw = now => {
      requestAnimationFrame(draw);
      if (!vis || !data) return;
      const rows = data.rows, n = rows.length, win = 600;
      const head = Math.floor(((now - start) / 1000) * data.hz) % n;
      ctx.clearRect(0, 0, w, h);
      const band = (k, y0, y1, lo, hi, color) => {
        ctx.strokeStyle = color; ctx.lineWidth = 1.8; ctx.beginPath();
        for (let i = 0; i < win; i++) {
          const r = rows[(head - win + i + n) % n];
          const v = (r[k] - lo) / (hi - lo);
          const x = i / (win - 1) * w, y = y1 - Math.max(0, Math.min(1, v)) * (y1 - y0);
          i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
        }
        ctx.stroke();
      };
      for (let i = 0; i < win; i++) {
        const r = rows[(head - win + i + n) % n];
        if (r[4]) { ctx.fillStyle = "rgba(242,182,43,0.9)"; ctx.fillRect(i / win * w - 1, 0, 3, h); }
        else if (data.states[r[3]] === "SHOCK_WINDOW") { ctx.fillStyle = "rgba(242,182,43,0.10)"; ctx.fillRect(i / win * w, 0, w / win + 1, h); }
      }
      band(0, 14, h * 0.5, 0, 62, "#e3122d");
      band(2, h * 0.56, h - 10, -1.6, 3.6, "#d7dce3");
      const r = rows[head];
      st.textContent = data.states[r[3]].replace(/_/g, " ");
    };
    requestAnimationFrame(draw);
  });
})();
