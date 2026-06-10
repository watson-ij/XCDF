# AGENTS.md

## Project context

XCDF is a C++11 binary data-format library used by HAWC. The repository contains
the core XCDF library, CLI utilities, Python bindings, correctness tests, and a
new performance benchmark harness.

A key constraint is **backward compatibility**: do not change the on-disk XCDF
format. HAWC has roughly a decade of data in this format, so performance work
should preserve the ability to read existing files byte-for-byte.

## Main performance goal

Improve read-side performance, especially for realistic HAWC raw trigger data and
older CPUs/batch systems. Broad, transparent improvements are preferred over
micro-optimizations unless profiling clearly justifies them.

Important read paths:

- sequential reads of scalar and vector fields
- vector-heavy HAWC trigger files
- random `Seek()` behavior
- concatenated files
- files without block tables/global trailers
- alias/expression evaluation
- stream-based input

## Benchmarking workflow

Use the Nix dev shell when possible:

```sh
nix develop --command cmake -S . -B build-nix -DBUILD_PYTHON=OFF -DCMAKE_BUILD_TYPE=Release -G Ninja
nix develop --command cmake --build build-nix
```

Run correctness tests before and after code changes:

```sh
nix develop --command ctest --test-dir build-nix --output-on-failure
```

Run the synthetic/feature benchmark suite:

```sh
nix develop --command ./build-nix/xcdf-perf-test --events 100000 --repetitions 5 --seek-count 10000 --output-dir /tmp --csv
```

For real HAWC trigger data, generate a local slice from the large trigger file:

```sh
bench/make_hawc_trig_slice.sh trig_run005113_00001.xcd perf-fixtures/hawc_trig_500k.xcd 500000
```

Then benchmark it:

```sh
nix develop --command ./build-nix/xcdf-perf-test \
  --scenario hawc_trig_fixture \
  --fixture perf-fixtures/hawc_trig_500k.xcd \
  --events 500000 \
  --repetitions 3 \
  --seek-count 10000 \
  --csv
```

Compare baseline/candidate CSVs with:

```sh
bench/compare_perf.py baseline.csv candidate.csv
```

The benchmark CSV includes metadata rows with XCDF version, git hash, compiler,
hostname, uname, CPU model, event count, seek count, and fixture path. Preserve
that metadata when sharing results between machines.

## Current local fixture policy

Large `.xcd` data files are ignored by git. Do not commit HAWC data files or
`perf-fixtures/` outputs. Benchmark fixtures should be regenerated locally from
available raw files.

## Coding guidelines

- Keep the on-disk XCDF format unchanged unless explicitly instructed otherwise.
- Prefer changes that are transparent to existing callers.
- Add/extend performance tests before relying on a performance claim.
- Preserve correctness checksums in `xcdf-perf-test` results.
- Run CTest after code changes.
- Avoid committing generated build directories or data files.

## Current broad optimization direction

The first broad read-side improvements added in this work are:

1. Fast intra-block seeking by bit-skipping uninterested fields/events while
   still decoding vector parent/count fields needed for child sizes.
2. Avoid active-max rediscovery during normal v3 reads when usable trailer
   globals are already loaded.

See `docs/performance_status.md` for measured HAWC trigger baseline/candidate
numbers and current status.
