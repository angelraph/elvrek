"""Elvrek: build the requirements traceability matrix and check every link.

Reads requirements/requirements.toml and the output of this run's host
scenarios, checks that each link is real, and writes REQUIREMENTS.md:

  code      the text is found in the file; the matrix links the line
  host      the scenario PASSed in this run; each "checks" text is an expect()
            in tests/host/scenarios.c, so the claim is the test's own words
  qnx       "PASS <case>" is in every QNX fault suite run listed under [runs]
  evidence  the pattern matches the result file (at least "min" times)

It also lists host scenarios and QNX cases that no requirement uses. It exits
non-zero if any link is broken or any test fails, so CI keeps the matrix true.

Usage: python3 tools/trace.py build/scenarios.txt [REQUIREMENTS.md]
"""
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    scen_out = Path(sys.argv[1]).read_text(encoding="utf-8")
    out_path = ROOT / (sys.argv[2] if len(sys.argv) > 2 else "REQUIREMENTS.md")
    spec = tomllib.loads((ROOT / "requirements/requirements.toml").read_text(encoding="utf-8"))
    scen_src = (ROOT / "tests/host/scenarios.c").read_text(encoding="utf-8")

    results = {name: status for status, name in re.findall(r"^(PASS|FAIL) (\S+)", scen_out, re.M)
               if name != "all"}
    if not results:
        sys.exit("trace: no scenario results in " + sys.argv[1])
    expects = set(re.findall(r'expect\([^;]*?"([^"]+)"\)', scen_src, re.S))
    runs = spec["runs"]["qnx"]
    run_text = {r: (ROOT / r).read_text(encoding="utf-8") for r in runs}
    qnx_cases_seen = set()
    for t in run_text.values():
        qnx_cases_seen |= set(re.findall(r"^PASS (T\d+)\b", t, re.M))

    errors, rows, used_scen, used_qnx = [], [], set(), set()
    for r in spec["req"]:
        rid = r["id"]
        cells = {"code": [], "host": [], "qnx": [], "evidence": []}
        verified = False

        for ref in r.get("code", []):
            path, _, text = ref.partition(":")
            f = ROOT / path
            if not f.exists():
                errors.append(f"{rid}: code file missing: {path}")
                continue
            lines = f.read_text(encoding="utf-8").splitlines()
            ln = next((i + 1 for i, l in enumerate(lines) if text in l), None)
            if ln is None:
                errors.append(f"{rid}: '{text}' not found in {path}")
                continue
            cells["code"].append(f"[`{path.split('/')[-1]}:{ln}`]({path}#L{ln})")

        host = r.get("host", [])
        for name in host:
            if name == "all":
                bad = [n for n, s in results.items() if s != "PASS"]
                if bad:
                    errors.append(f"{rid}: scenarios failing: {', '.join(bad)}")
                cells["host"].append(f"all {len(results)} scenarios PASS")
                used_scen |= set(results)
            elif results.get(name) == "PASS":
                cells["host"].append(f"`{name}` PASS")
                used_scen.add(name)
            else:
                errors.append(f"{rid}: scenario {name} is {results.get(name, 'missing')}")
        for c in r.get("checks", []):
            if c not in expects:
                errors.append(f"{rid}: check text not an expect() in scenarios.c: {c}")
        if host:
            verified = True     # failures above are already counted as broken

        for case in r.get("qnx", []):
            ok = [run for run in runs if re.search(r"^PASS " + re.escape(case), run_text[run], re.M)]
            if len(ok) != len(runs):
                errors.append(f"{rid}: QNX case '{case}' passes in {len(ok)} of {len(runs)} runs")
            cells["qnx"].append(f"{case}: PASS in {len(ok)} of {len(runs)} runs")
            used_qnx.add(case.split()[0])
            verified = True

        for ev in r.get("evidence", []):
            f = ROOT / ev["file"]
            n = len(re.findall(ev["match"], f.read_text(encoding="utf-8"))) if f.exists() else 0
            need = ev.get("min", 1)
            if n < need:
                errors.append(f"{rid}: evidence '{ev['match']}' found {n} times in {ev['file']}, need {need}")
            name = ev["file"].split("/")[-1]
            cells["evidence"].append(f"[{name}]({ev['file']})" + (f" ({n} of {need} needed)" if need > 1 else ""))
            verified = True

        if not verified:
            errors.append(f"{rid}: no test or result verifies it")
        rows.append((r, cells))

    unused_scen = sorted(set(results) - used_scen)
    unused_qnx = sorted(qnx_cases_seen - used_qnx, key=lambda s: int(s[1:]))
    write(out_path, spec, rows, results, runs, unused_scen, unused_qnx, errors)
    total = len(rows)
    bad_ids = {e.split(":")[0] for e in errors}
    print(f"trace: {total - len(bad_ids)} of {total} requirements traced and verified")
    for e in errors:
        print("  BROKEN " + e)
    return 1 if errors else 0


def write(path, spec, rows, results, runs, unused_scen, unused_qnx, errors):
    bad_ids = {e.split(":")[0] for e in errors}
    total = len(rows)
    L = []
    L.append("# Requirements, traced to code, tests and QNX results")
    L.append("")
    L.append("Generated by [`tools/trace.py`](tools/trace.py) from "
             "[`requirements/requirements.toml`](requirements/requirements.toml) on every `make trace` and in CI. "
             "Do not edit by hand. Every link below was checked when this file was written: the code text exists at "
             "that line, the host scenario passed in that run and the check named is one of its own `expect()` lines, "
             "the QNX case passed in every listed run, and the result file holds the pattern.")
    L.append("")
    L.append(f"**{total - len(bad_ids)} of {total} requirements traced and verified.** "
             f"Host scenarios in this run: {sum(1 for s in results.values() if s == 'PASS')} of {len(results)} pass. "
             f"QNX runs checked: {', '.join('[' + r.split('/')[-1] + '](' + r + ')' for r in runs)}.")
    L.append("")
    if errors:
        L.append("## Broken links")
        L.append("")
        L += [f"* {e}" for e in errors]
        L.append("")
    group = None
    for r, c in rows:
        if r["group"] != group:
            group = r["group"]
            L.append(f"## {group}")
            L.append("")
            L.append("| ID | Requirement | Code | Host test | On QNX 8.0 | Other result |")
            L.append("|---|---|---|---|---|---|")
        status = "" if r["id"] not in bad_ids else " **BROKEN**"
        req = f"{r['text']}<br><sub>{r['why']}</sub>"
        if r.get("checks"):
            req += "<br><sub>Checks: " + "; ".join(f"“{x}”" for x in r["checks"]) + "</sub>"
        if r.get("limit"):
            req += f"<br><sub>**Limit:** {r['limit']}</sub>"
        L.append(f"| **{r['id']}**{status} | {req} | {'<br>'.join(c['code']) or '-'} | "
                 f"{'<br>'.join(c['host']) or '-'} | {'<br>'.join(c['qnx']) or '-'} | {'<br>'.join(c['evidence']) or '-'} |")
        nxt = rows.index((r, c)) + 1
        if nxt == len(rows) or rows[nxt][0]["group"] != group:
            L.append("")
    L.append("## Tests no requirement uses")
    L.append("")
    L.append("Host scenarios: " + (", ".join(f"`{s}`" for s in unused_scen) if unused_scen else "none") + ".")
    L.append("")
    L.append("QNX fault suite cases: " + (", ".join(unused_qnx) if unused_qnx else "none") + ".")
    L.append("")
    L.append("## Not claimed")
    L.append("")
    L.append("This is a research controller tested against a physics model and on QNX 8.0 in QEMU. "
             "It is not a medical device, has not been used on people, and holds no certification. "
             "A requirement here means the stated test passes on that model and target, nothing more.")
    L.append("")
    path.write_text("\n".join(L), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    sys.exit(main())
