#!/usr/bin/env python3
"""Write the short prose pages of the Elvrek site from one template.

Usage: python3 -I tools/make_legal_pages.py
"""
import os

HEAD = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title} | Elvrek</title>
<meta name="description" content="{desc}">
<meta name="theme-color" content="#07080a">
<link rel="icon" href="/assets/elvrek-logo.webp">
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@600;700&family=Inter:wght@400;500;600&display=swap" rel="stylesheet">
<link rel="stylesheet" href="/assets/css/site.css?v=202611191401">
</head>
<body data-page="{page}">
<div data-header></div>
<main>
  <section class="page-hero">
    <canvas class="hero-bg" data-mode="soft" aria-hidden="true"></canvas>
    <div class="wrap"><span class="eyebrow">{eyebrow}</span><h1>{h1}</h1><p class="lead">{lead}</p></div>
  </section>
  <section class="section"><div class="narrow prose reveal">
{body}
  </div></section>
</main>
<div data-footer></div>
<script src="/assets/js/site.js?v=202611191401"></script>
</body>
</html>
"""

PAGES = {
    "safety": dict(
        title="Safety notice", desc="Elvrek is research software and not a medical device.",
        eyebrow="Safety notice", h1='Not a <span class="red">medical device</span>',
        lead="Please read this before you build, run or demonstrate Elvrek.",
        body="""
<h2>Intended use</h2>
<p>Elvrek is research and teaching software for studying how a real-time controller for mechanical CPR can be built and tested. It is intended for engineers, researchers and trainers working with simulations, emulators and training manikins.</p>
<h2>What it is not</h2>
<ul class="squares">
<li>It is not a medical device and has no regulatory clearance, approval or certification in any country.</li>
<li>It has not been validated for clinical use and must never be connected to, or used on, a person or an animal.</li>
<li>It cannot help in a real emergency. In an emergency, call your local emergency number and follow the dispatcher's instructions.</li>
</ul>
<h2>Bench and manikin work</h2>
<ul class="squares">
<li>Any physical build must keep a mechanical stop behind the software depth limit and a guard that keeps hands away from the piston.</li>
<li>The shock output must drive a lamp or an indicator only. Never connect it to a real defibrillator.</li>
<li>Always keep a hardware emergency stop within reach.</li>
</ul>
<h2>Known limitations</h2>
<p>The rhythm checks do not yet meet the American Heart Association performance goals on real ECG. The piston, the chest and the patient's response are physics models. All results are reported with these limits in mind. See the <a href="/evidence">evidence page</a> for details.</p>
"""),
    "privacy": dict(
        title="Privacy", desc="How the Elvrek website handles your data.",
        eyebrow="Privacy", h1='Your <span class="red">privacy</span>',
        lead="The short version: this site does not track you.",
        body="""
<h2>What we collect</h2>
<p>Nothing on purpose. The site has no accounts, no analytics, no advertising and no tracking cookies.</p>
<h2>Third parties</h2>
<ul class="squares">
<li>The site is hosted on Vercel, which keeps standard server logs to run and protect its service.</li>
<li>Fonts are loaded from Google Fonts, which receives your IP address when your browser requests them.</li>
<li>The contact form does not send anything to us. It opens a pre-filled GitHub issue, and nothing is published until you submit it on GitHub, under GitHub's own terms and privacy policy.</li>
</ul>
<h2>Questions</h2>
<p>If you have a question about privacy, please <a href="/contact">get in touch</a>.</p>
"""),
    "terms": dict(
        title="Terms of use", desc="Terms for using the Elvrek website and software.",
        eyebrow="Terms of use", h1='Terms of <span class="red">use</span>',
        lead="Plain terms for a research project.",
        body="""
<h2>The software</h2>
<p>The Elvrek source code is licensed under the Apache License 2.0. It is provided as is, without warranties or conditions of any kind. You are responsible for how you use it.</p>
<h2>No medical use</h2>
<p>By using the website or the software you agree not to use Elvrek on a person or an animal, or in any situation where a failure could cause injury. See the <a href="/safety">safety notice</a>.</p>
<h2>Data</h2>
<p>Evaluation data comes from PhysioNet under the Open Data Commons Attribution License, and is credited in the repository's data notice.</p>
<h2>Names and marks</h2>
<p>QNX is a trademark of BlackBerry Limited. Other product names mentioned on this site belong to their owners and are used only to describe compatibility or published research.</p>
"""),
}


def main():
    out = os.path.join(os.path.dirname(__file__), "..", "web")
    for page, d in PAGES.items():
        html = HEAD.format(page=page, **d)
        with open(os.path.join(out, page + ".html"), "w", encoding="utf-8") as f:
            f.write(html)
    nf = HEAD.format(page="404", title="Page not found", desc="Page not found.",
                     eyebrow="Error 404", h1='Lost the <span class="red">rhythm</span>',
                     lead="This page does not exist, or it moved.",
                     body='<p><a class="btn btn-red" href="/" style="border:0">Back to the start</a></p>')
    with open(os.path.join(out, "404.html"), "w", encoding="utf-8") as f:
        f.write(nf)
    print("wrote", ", ".join(list(PAGES) + ["404"]))


if __name__ == "__main__":
    main()
