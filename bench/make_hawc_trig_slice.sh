#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "Usage: $0 INPUT_TRIG_XCDF OUTPUT_SLICE_XCDF [EVENTS]" >&2
  echo "Example: $0 trig_run005113_00001.xcd perf-fixtures/hawc_trig_100k.xcd 100000" >&2
  exit 2
fi

input=$1
output=$2
events=${3:-100000}

mkdir -p "$(dirname "$output")"

if [[ ! -x ./build-nix/xcdf ]]; then
  echo "Expected ./build-nix/xcdf. Build first:" >&2
  echo "  nix develop --command cmake -S . -B build-nix -DBUILD_PYTHON=OFF -DCMAKE_BUILD_TYPE=Release -G Ninja" >&2
  echo "  nix develop --command cmake --build build-nix" >&2
  exit 2
fi

# currentEventNumber is zero-based. Keep a deterministic prefix so benchmark
# fixtures can be regenerated locally without committing large data files.
nix develop --command ./build-nix/xcdf select "currentEventNumber < ${events}" -o "$output" "$input"

nix develop --command ./build-nix/xcdf info "$output" | tail -20
