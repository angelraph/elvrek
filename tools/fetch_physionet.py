#!/usr/bin/env python3
"""
Elvrek: fetch and convert open PhysioNet arrest ECG for evaluation.
SPDX-License-Identifier: Apache-2.0

Databases (Open Data Commons Attribution License v1.0, credited in DATA-NOTICE.md):
  cudb  Creighton University Ventricular Tachyarrhythmia Database, 35 records
  vfdb  MIT-BIH Malignant Ventricular Ectopy Database, 22 records

Usage:
  python3 -I tools/fetch_physionet.py --data data/physionet --out build/ecg

Downloads the raw WFDB files (.hea, .dat, .atr) into --data, then writes one
file per record into --out:
  'ELVE' | u32 fs | u32 n | float32[n] mV (first channel) | uint8[n] label
Labels: 0 excluded (noise/unknown), 1 shockable (VF, flutter),
        2 VT (intermediate, reported only), 3 normal sinus,
        4 other non-shockable, 5 asystole.
Standard library only. Parses WFDB format 212 and MIT annotation files.
"""
import argparse
import os
import struct
import sys
import urllib.request

BASE = "https://physionet.org/files/{db}/1.0.0/{name}"
DBS = ("cudb", "vfdb")

EXCL, SHOCK, VT, NSR, OTHER, ASYS = 0, 1, 2, 3, 4, 5
RHYTHM_MAP = {
    "(VF": SHOCK, "(VFL": SHOCK, "(VFIB": SHOCK,
    "(VT": VT,
    "(N": NSR, "(NSR": NSR,
    "(ASYS": ASYS,
    "(NOISE": EXCL, "(NOD": OTHER,
}


def fetch(db, name, dest):
    path = os.path.join(dest, name)
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return path
    url = BASE.format(db=db, name=name)
    req = urllib.request.Request(url, headers={"User-Agent": "elvrek-research-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=60) as r, open(path + ".part", "wb") as f:
        f.write(r.read())
    os.replace(path + ".part", path)
    return path


def read_header(path):
    with open(path) as f:
        lines = [l.strip() for l in f if l.strip() and not l.startswith("#")]
    rec = lines[0].split()
    nsig, fs, nsamp = int(rec[1]), float(rec[2].split("/")[0]), int(rec[3])
    sigs = []
    for l in lines[1:1 + nsig]:
        p = l.split()
        fmt = p[1].split("x")[0].split(":")[0]
        gs = p[2]
        gain = float(gs.split("(")[0].split("/")[0]) or 200.0
        baseline = None
        if "(" in gs:
            baseline = int(gs.split("(")[1].split(")")[0])
        adczero = int(p[4]) if len(p) > 4 else 0
        sigs.append({"file": p[0], "fmt": fmt, "gain": gain,
                     "baseline": adczero if baseline is None else baseline})
    return nsig, fs, nsamp, sigs


def read_212(path, nsig, nsamp):
    raw = open(path, "rb").read()
    vals = []
    for i in range(0, len(raw) - 2, 3):
        b0, b1, b2 = raw[i], raw[i + 1], raw[i + 2]
        s0 = b0 | ((b1 & 0x0F) << 8)
        s1 = b2 | ((b1 & 0xF0) << 4)
        if s0 > 2047: s0 -= 4096
        if s1 > 2047: s1 -= 4096
        vals.append(s0)
        vals.append(s1)
    n = min(nsamp, len(vals) // nsig)
    return [vals[k * nsig] for k in range(n)]      # first channel


def read_annotations(path):
    """Yield (sample, type, aux) from a MIT-format annotation file."""
    data = open(path, "rb").read()
    out, t, i = [], 0, 0
    while i + 1 < len(data):
        w = data[i] | (data[i + 1] << 8)
        i += 2
        a, iv = w >> 10, w & 0x3FF
        if a == 0 and iv == 0:
            break
        if a == 59:                                  # SKIP: 32-bit PDP-11 long
            hi = data[i] | (data[i + 1] << 8)
            lo = data[i + 2] | (data[i + 3] << 8)
            skip = (hi << 16) | lo
            if skip & 0x80000000: skip -= 1 << 32
            t += skip
            i += 4
        elif a == 63:                                # AUX string for previous
            s = data[i:i + iv].split(b"\0")[0].decode("ascii", "replace")
            if out:
                out[-1] = (out[-1][0], out[-1][1], s)
            i += iv + (iv & 1)
        elif a in (60, 61, 62):                      # NUM, SUB, CHN
            pass
        else:
            t += iv
            out.append((t, a, ""))
    return out


def labels_for(db, n, anns):
    lab = bytearray([EXCL]) * n
    if db == "vfdb":
        cur, start = EXCL, 0
        for (t, a, aux) in anns:
            if a == 28 and aux:                      # rhythm change
                code = RHYTHM_MAP.get(aux.strip(), OTHER)
                for k in range(start, min(t, n)): lab[k] = cur
                cur, start = code, t
        for k in range(start, n): lab[k] = cur
    else:                                            # cudb: VF episodes [ ... ]
        cur, start = OTHER, 0
        for (t, a, aux) in anns:
            if a in (32, 33):
                for k in range(start, min(t, n)): lab[k] = cur
                cur, start = (SHOCK if a == 32 else OTHER), t
            elif a == 28 and aux.strip() in RHYTHM_MAP:
                for k in range(start, min(t, n)): lab[k] = cur
                cur, start = RHYTHM_MAP[aux.strip()], t
        for k in range(start, n): lab[k] = cur
    # noise annotations (~) mark 1 s around them as excluded
    for (t, a, aux) in anns:
        if a == 14:
            for k in range(max(0, t - 125), min(n, t + 125)): lab[k] = EXCL
    return lab


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="data/physionet")
    ap.add_argument("--out", default="build/ecg")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    total = {db: 0 for db in DBS}
    for db in DBS:
        dest = os.path.join(args.data, db)
        os.makedirs(dest, exist_ok=True)
        recs = open(fetch(db, "RECORDS", dest)).read().split()
        for rec in recs:
            for ext in (".hea", ".dat", ".atr"):
                fetch(db, rec + ext, dest)
            nsig, fs, nsamp, sigs = read_header(os.path.join(dest, rec + ".hea"))
            if sigs[0]["fmt"] != "212":
                print(f"skip {db}/{rec}: format {sigs[0]['fmt']}", file=sys.stderr)
                continue
            adu = read_212(os.path.join(dest, sigs[0]["file"]), nsig, nsamp)
            g, b = sigs[0]["gain"], sigs[0]["baseline"]
            mv = [(v - b) / g for v in adu]
            lab = labels_for(db, len(mv), read_annotations(os.path.join(dest, rec + ".atr")))
            with open(os.path.join(args.out, f"{db}_{rec}.bin"), "wb") as f:
                f.write(b"ELVE" + struct.pack("<II", int(fs), len(mv)))
                f.write(struct.pack(f"<{len(mv)}f", *mv))
                f.write(bytes(lab))
            counts = [lab.count(c) for c in range(6)]
            print(f"{db}/{rec}: fs={fs:.0f} n={len(mv)} VF={counts[1]} VT={counts[2]} "
                  f"NSR={counts[3]} other={counts[4]} asys={counts[5]} excl={counts[0]}")
            total[db] += 1
    print("records converted:", total)


if __name__ == "__main__":
    main()
