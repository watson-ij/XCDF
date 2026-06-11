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

## Nix commands: do not hand-expand build commands

Use flake apps. They configure/build into `build-nix` automatically and keep
CMake/Ninja chatter on stderr so benchmark CSV redirected from stdout stays
parseable.

```sh
nix run .#build   # configure + build
nix run .#test    # configure + build + ctest --output-on-failure
nix run .#perf -- [xcdf-perf-test args]
```

Override the build dir if needed:

```sh
XCDF_BUILD_DIR=/tmp/xcdf-build nix run .#test
```

Run correctness tests before and after code changes:

```sh
nix run .#test
```

Run the synthetic/feature benchmark suite:

```sh
nix run .#perf -- --events 100000 --repetitions 5 --seek-count 10000 --output-dir /tmp --csv > candidate.csv
```

For real HAWC trigger data, generate a local slice from the large trigger file:

```sh
bench/make_hawc_trig_slice.sh trig_run005113_00001.xcd perf-fixtures/hawc_trig_500k.xcd 500000
```

Then benchmark it:

```sh
nix run .#perf -- \
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

## Existing benchmark CSVs and fixture workflow

This checkout may already have local, ignored benchmark fixtures/results under
`perf-fixtures/`. Check there before regenerating anything:

```sh
ls perf-fixtures
```

Known useful local files in this working tree have included:

- `perf-fixtures/hawc_trig_500k.xcd`
- `perf-fixtures/hawc_trig_500k_baseline.csv`
- `perf-fixtures/hawc_trig_500k_combined_candidate.csv`
- other candidate CSVs from intermediate optimization attempts

Compare a new candidate against the saved baseline with:

```sh
nix run .#perf -- \
  --scenario hawc_trig_fixture \
  --fixture perf-fixtures/hawc_trig_500k.xcd \
  --events 500000 \
  --repetitions 3 \
  --seek-count 10000 \
  --csv > perf-fixtures/hawc_trig_500k_current_candidate.csv

bench/compare_perf.py \
  perf-fixtures/hawc_trig_500k_baseline.csv \
  perf-fixtures/hawc_trig_500k_current_candidate.csv
```

The comparison script warns if metadata differs, including git hash/version.
That is expected when comparing a committed baseline to a dirty candidate, but
not acceptable for cross-machine comparisons unless intentionally noted.

## Current local fixture policy

Large `.xcd` data files and generated benchmark CSVs are ignored by git. Do not
commit HAWC data files or `perf-fixtures/` outputs. Benchmark fixtures should be
regenerated locally from available raw files when missing.

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
