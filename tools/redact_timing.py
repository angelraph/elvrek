"""Elvrek: withhold measurements of QNX itself from the published result logs.

On 28 Nov 2026 the QNX Global Hackathon organisers asked that entries do not
include performance evaluations of QNX software (licence section 4.6(i)),
while performance of an entrant's own software may be published. This script
replaces, in place, the values in our raw logs that measure the platform:
tick delivery and missed ticks, late wake-ups, boot times, and kernel trace
figures for timer pulses, wake-ups and scheduling. PASS and FAIL lines,
counts from our own tests and the work done by our own code are unchanged.
Each file gets a note saying what was withheld.

Usage: python tools/redact_timing.py        (idempotent)
"""
import gzip
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NOTE = ("# Timing of QNX itself withheld (tick delivery, missed ticks, wake-ups, boot times),\n"
        "# per the organisers' guidance on licence section 4.6(i), 28 Nov 2026. Results unchanged.\n")
W = "-"

SUBS = [
    (r"control loop ticking \(\d+ ticks in 1 s\)", "control loop ticking"),
    (r"only \d+ ticks in 1 s", "too few ticks in 1 s"),
    (r",? ?\(missed ticks \d+ with storm, \d+ without, host noise\)", ""),
    (r"missed ticks \d+ with storm vs \d+ without, ", ""),
    (r"missed \d+ with storm vs \d+ without, ", ""),
    (r", missed \d+ vs \d+", ""),
    (r" missed \d+( inv)", r"\1"),
    (r" missed \d+$", ""),
    (r"\b(missed|late|overrun)=\d+", r"\1=" + W),
    (r"\b(boot_proc_ms|boot_ready_ms|boot_start_ms|boot_comp_ms|ssh_answered_host_ms)=\d+", r"\1=" + W),
    (r"\b(controller_process|control_loop|status_seen|start|first_compression)=\d+(?= )", r"\1=" + W),
    (r"(?m)^ +\d+\.\d{3}  ", "  "),            # host timestamps on boot logs
]


def redact(text):
    for a, b in SUBS:
        text = re.sub(a, b, text, flags=re.M)
    return text


def main():
    changed = 0
    for p in sorted((ROOT / "docs/results").rglob("*.txt")):
        t = p.read_text(encoding="utf-8")
        r = redact(t)
        if "Timing of QNX itself withheld" not in r and r != t:
            r = NOTE + r
        if r != t:
            p.write_text(r, encoding="utf-8", newline="\n")
            changed += 1
            print("redacted", p.relative_to(ROOT))
    gz = ROOT / "docs/results/qnx-console-log.txt.gz"
    if gz.exists():
        t = gzip.decompress(gz.read_bytes()).decode("utf-8")
        r = redact(t)
        if r != t:
            gz.write_bytes(gzip.compress(r.encode("utf-8"), 9, mtime=0))
            changed += 1
            print("redacted", gz.relative_to(ROOT))
    print(changed, "files changed")


if __name__ == "__main__":
    main()
