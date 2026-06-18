#!/usr/bin/env python3
"""Compare xcdf-perf-test CSV outputs.

The benchmark emits comment-style metadata rows followed by normal CSV rows.
This script ignores metadata for numeric summaries, but prints metadata that
differs between baseline and candidate so accidental cross-machine or cross-build
comparisons are visible.
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Tuple


@dataclass
class Summary:
    count: int
    median_seconds: float
    min_seconds: float
    max_seconds: float
    median_events_per_second: float
    median_mib_per_second: float
    checksum: str


Key = Tuple[str, str]


def read_perf_csv(path: Path):
    metadata: Dict[str, str] = {}
    rows: List[dict] = []
    csv_lines: List[str] = []

    with path.open(newline="") as f:
        for line in f:
            if line.startswith("#"):
                payload = line[1:].strip()
                if "," in payload:
                    key, value = payload.split(",", 1)
                    metadata[key.strip()] = value.strip()
            elif line.strip():
                csv_lines.append(line)

    if not csv_lines:
        raise SystemExit(f"No CSV rows found in {path}")

    reader = csv.DictReader(csv_lines)
    for row in reader:
        rows.append(row)
    return metadata, rows


def summarize(rows: Iterable[dict]) -> Dict[Key, Summary]:
    grouped: Dict[Key, List[dict]] = defaultdict(list)
    for row in rows:
        grouped[(row["scenario"], row["operation"])].append(row)

    out: Dict[Key, Summary] = {}
    for key, values in grouped.items():
        seconds = [float(v["seconds"]) for v in values]
        event_rates = [float(v["events_per_second"]) for v in values]
        mib_rates = [float(v["mib_per_second"]) for v in values]
        checksums = sorted(set(v["checksum"] for v in values))
        checksum = checksums[0] if len(checksums) == 1 else "MISMATCH:" + ";".join(checksums)
        out[key] = Summary(
            count=len(values),
            median_seconds=statistics.median(seconds),
            min_seconds=min(seconds),
            max_seconds=max(seconds),
            median_events_per_second=statistics.median(event_rates),
            median_mib_per_second=statistics.median(mib_rates),
            checksum=checksum,
        )
    return out


def print_metadata_diff(base_meta: Dict[str, str], cand_meta: Dict[str, str]):
    interesting = [
        "xcdf_perf_schema",
        "xcdf_version",
        "xcdf_git_hash",
        "compiler",
        "cplusplus",
        "hostname",
        "uname",
        "cpu_model",
        "events",
        "repetitions",
        "seek_count",
        "scenario",
        "fixture",
    ]
    diffs = []
    for key in interesting:
      b = base_meta.get(key, "")
      c = cand_meta.get(key, "")
      if b != c:
          diffs.append((key, b, c))

    if diffs:
        print("Metadata differences:")
        for key, b, c in diffs:
            print(f"  {key}: baseline={b!r} candidate={c!r}")
        print()


def print_single(summary: Dict[Key, Summary]):
    print(f"{'scenario':24} {'operation':18} {'n':>4} {'median_s':>12} {'events/s':>14} {'MiB/s':>12} checksum")
    for key in sorted(summary):
        s = summary[key]
        print(f"{key[0]:24} {key[1]:18} {s.count:4d} {s.median_seconds:12.6g} "
              f"{s.median_events_per_second:14.3f} {s.median_mib_per_second:12.3f} {s.checksum}")


def print_comparison(base: Dict[Key, Summary], cand: Dict[Key, Summary], threshold: float):
    print(f"{'scenario':24} {'operation':18} {'base_s':>12} {'cand_s':>12} {'delta':>9} {'speedup':>9} {'status':>10}")
    all_keys = sorted(set(base) | set(cand))
    for key in all_keys:
        if key not in base:
            print(f"{key[0]:24} {key[1]:18} {'missing':>12} {cand[key].median_seconds:12.6g} {'':>9} {'':>9} {'new':>10}")
            continue
        if key not in cand:
            print(f"{key[0]:24} {key[1]:18} {base[key].median_seconds:12.6g} {'missing':>12} {'':>9} {'':>9} {'removed':>10}")
            continue

        b = base[key]
        c = cand[key]
        delta = (c.median_seconds - b.median_seconds) / b.median_seconds if b.median_seconds else 0.0
        speedup = b.median_seconds / c.median_seconds if c.median_seconds else float("inf")
        status = "same"
        if delta <= -threshold:
            status = "faster"
        elif delta >= threshold:
            status = "slower"
        checksum = ""
        if b.checksum != c.checksum:
            checksum = " checksum!"
        print(f"{key[0]:24} {key[1]:18} {b.median_seconds:12.6g} {c.median_seconds:12.6g} "
              f"{delta:+8.2%} {speedup:8.3f}x {status:>10}{checksum}")


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path, nargs="?")
    parser.add_argument("--threshold", type=float, default=0.02,
                        help="fractional delta treated as meaningful (default: 0.02)")
    args = parser.parse_args(argv)

    base_meta, base_rows = read_perf_csv(args.baseline)
    base_summary = summarize(base_rows)

    if args.candidate is None:
        print_single(base_summary)
        return 0

    cand_meta, cand_rows = read_perf_csv(args.candidate)
    cand_summary = summarize(cand_rows)
    print_metadata_diff(base_meta, cand_meta)
    print_comparison(base_summary, cand_summary, args.threshold)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
