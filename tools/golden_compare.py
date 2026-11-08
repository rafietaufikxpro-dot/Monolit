#!/usr/bin/env python3
# Monolit - offline Pokemon bank for Switch CFW
# Copyright (C) 2026 rafietaufik
# SPDX-License-Identifier: GPL-3.0-only
"""Compares Monolit's converter output (mine.hex) with PKHeX's (pkhex.hex), byte by byte, and summarises diffs."""
import sys, collections
# Ranges PKHeX fills from its legality engine (LegalityRejuvenator), which Monolit deliberately does not guess:
# relearn moves (PK8/PB8 0x82-0x89, PA8 0x8A-0x91) and Legends: Arceus move-mastery flags (PA8 0x15D-0x164).
IGNORE = set(range(0x82, 0x8A)) | set(range(0x8A, 0x92)) | set(range(0x15D, 0x165)) if "--ignore-legality" in sys.argv else set()
args = [a for a in sys.argv[1:] if not a.startswith("--")]
inp = open(args[2]).read().splitlines() if len(args) > 2 else None
mine = [l.split(" ", 1) for l in open(args[0]).read().splitlines()]
ref = [l.split(" ", 1) for l in open(args[1]).read().splitlines()]
diff_offsets = collections.Counter(); status = collections.Counter(); exact = 0
for i, (m, r) in enumerate(zip(mine, ref)):
    status[(m[0], r[0])] += 1
    if m[0] == "OK" and r[0] == "OK":
        a, b = bytes.fromhex(m[1]), bytes.fromhex(r[1])
        d = [k for k in range(min(len(a), len(b))) if a[k] != b[k] and k not in (6, 7) and k not in IGNORE]  # checksum differs if anything differs
        if not d: exact += 1
        for k in d: diff_offsets[k] += 1
print("status (mine, pkhex):", dict(status))
print("exact matches:", exact, "of", sum(v for (m, r), v in status.items() if m == r == "OK"))
for k, v in sorted(diff_offsets.items()): print(f"  offset 0x{k:03X}: differs in {v} cases")
