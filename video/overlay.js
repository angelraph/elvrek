(() => {
  if (window.__ov) return;
  const css = `
  #__ov{position:fixed;inset:0;pointer-events:none;z-index:2147483647;font-family:Inter,system-ui,sans-serif}
  #__cur{position:absolute;left:0;top:0;width:34px;height:34px;opacity:0;filter:drop-shadow(0 3px 6px rgba(0,0,0,.45))}
  #__rip{position:absolute;width:70px;height:70px;margin:-35px 0 0 -35px;border-radius:50%;
    border:3px solid rgba(225,29,46,.9);opacity:0}
  #__chip{position:absolute;left:64px;top:118px;display:flex;align-items:center;gap:14px;opacity:0;
    background:rgba(7,8,10,.78);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,.12);
    border-left:4px solid #e11d2e;padding:14px 22px 14px 18px;border-radius:10px;color:#fff}
  #__chip b{font:700 15px/1 'Barlow Condensed',sans-serif;letter-spacing:.18em;color:#e11d2e}
  #__chip span{font:600 22px/1.1 Inter,sans-serif}
  `;
  const st = document.createElement('style'); st.textContent = css; document.head.appendChild(st);
  const ov = document.createElement('div'); ov.id = '__ov';
  ov.innerHTML = `<div id="__rip"></div><div id="__chip"><b></b><span></span></div>
   <svg id="__cur" viewBox="0 0 24 24"><path d="M4 2l15 10.5-6.6 1.2 3.9 7.4-3 1.5-3.8-7.4L4 20z"
   fill="#fff" stroke="#111" stroke-width="1.3" stroke-linejoin="round"/></svg>`;
  document.body.appendChild(ov);
  const $ = id => document.getElementById(id);
  window.__ov = {
    render(s) {
      const c = $('__cur');
      if (s.cursor) {
        c.style.opacity = s.cursor.o ?? 1;
        c.style.transform = `translate(${s.cursor.x - 5}px,${s.cursor.y - 3}px) scale(${s.cursor.s ?? 1})`;
      } else c.style.opacity = 0;
      const r = $('__rip');
      if (s.ripple) {
        const p = s.ripple.p;              // 0..1
        r.style.left = s.ripple.x + 'px'; r.style.top = s.ripple.y + 'px';
        r.style.opacity = (1 - p) * 0.9;
        r.style.transform = `scale(${0.3 + p * 1.1})`;
      } else r.style.opacity = 0;
      const ch = $('__chip');
      if (s.chip) {
        ch.querySelector('b').textContent = s.chip.k;
        ch.querySelector('span').textContent = s.chip.t;
        ch.style.opacity = s.chip.o;
        ch.style.transform = `translateX(${(1 - s.chip.o) * -40}px)`;
      } else ch.style.opacity = 0;
    }
  };
})();
