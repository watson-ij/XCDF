# XCDF performance benchmarking

See `docs/performance_status.md` for the current HAWC trigger benchmark status,
latest measured baseline/candidate numbers, and current optimization notes.

This repository includes `xcdf-perf-test`, a repeatable C++ benchmark driver for
read/write/seek performance.  It intentionally exercises several data shapes and
compatibility features instead of a single synthetic speed test.

The harness lives in `tests/perf/`:

- `PerfMain.cc`: CLI, timing, and reporting.
- `PerfUtil.cc`: shared file/seek helpers.
- `PerfCoreScenarios.cc`: common IO/data-shape benchmarks.
- `PerfFeatureScenarios.cc`: compatibility and feature-sensitive benchmarks.
- `PerfTest.h`: shared scenario definitions.

## Build under Nix

```sh
nix develop --command cmake -S . -B build-nix -DBUILD_PYTHON=OFF -DCMAKE_BUILD_TYPE=Release -G Ninja
nix develop --command cmake --build build-nix
```

## Run the benchmark suite

```sh
nix develop --command ./build-nix/xcdf-perf-test --events 1000000 --repetitions 5 --seek-count 50000 --output-dir /tmp --csv
```

Use `--list` to see scenarios and operations, and `--scenario NAME` to focus on
one case. Generated files are removed by default; add `--keep-files` if you want
to inspect file sizes or profile subsequent reads manually.

## Real HAWC trigger fixture

Large real data files should not be committed. If you have a local trigger file,
create a deterministic prefix slice and benchmark it as a fixture:

```sh
bench/make_hawc_trig_slice.sh trig_run005113_00001.xcd perf-fixtures/hawc_trig_100k.xcd 100000

nix develop --command ./build-nix/xcdf-perf-test \
  --scenario hawc_trig_fixture \
  --fixture perf-fixtures/hawc_trig_100k.xcd \
  --events 100000 \
  --repetitions 10 \
  --seek-count 10000 \
  --csv
```

For the full local file, pass it directly as `--fixture` and set `--events` to
how many leading events you want each measured read operation to consume.

## Core data-shape scenarios

- `speed_mix`: the original `SpeedTest.cc` shape: scalar integers, scalar double,
  and a fixed-length vector.
- `scalar_ints`: integer-heavy event metadata / detector quantities.
- `quantized_doubles`: several lossy floating-point reconstruction quantities.
- `vector_variable`: variable-length hit arrays with integer and floating-point
  child fields.
- `many_fields`: stresses per-field overhead with many scalar fields.
- `full_precision`: unquantized 64-bit floating-point fields.

## Feature and compatibility scenarios

- `small_blocks`: many block transitions and a larger block table.
- `large_blocks`: large compressed blocks and intra-block seeking/skipping.
- `no_block_table`: compatibility path for files without block table/global data;
  event counts and global ranges require scans.
- `many_comments`: trailer/comment metadata loading.
- `aliases`: header/trailer aliases and expression evaluation during reads.
- `nested_vectors`: recursive vector parentage and a vector-reduction alias.
- `concatenated`: byte-for-byte concatenated XCDF files with remapped block-table
  offsets.
- `istream_read`: user-supplied `std::istream` reads vs XCDF-owned file streams.
- `hawc_trig_fixture`: existing real HAWC trigger file or slice supplied with
  `--fixture`; measures full vector-heavy reads, scalar-only reads, seeks, event
  count, and global range metadata.

Each scenario reports `write` plus one or more read-side operations. Output
columns include elapsed seconds, events/s, MiB/s based on the final XCDF file
size, bytes/event, and a checksum to discourage dead-code elimination and catch
obvious behavioral problems.

## Suggested comparison workflow

1. Build `main`/baseline in Release mode.
2. Run the CSV command above and save the output, e.g. `baseline.csv`.
3. Apply an optimization.
4. Rebuild and rerun with the same command, saving `candidate.csv`.
5. Compare median times per `(scenario, operation)` before deciding whether to
   keep the change:

```sh
bench/compare_perf.py baseline.csv candidate.csv
```

Use `bench/compare_perf.py baseline.csv` to summarize a single run.

Recommended acceptance gate before keeping a code change:

- same XCDF git hash baseline/candidate except for the candidate patch under test
- same fixture path and event/seek counts
- same checksums for every operation
- median target read operations improve by at least ~5% or explain why a smaller
  win matters
- no important read/write/seek scenario regresses by more than ~1-2%
- final validation includes a large slice or the full HAWC trigger file on the
  relevant UMD batch CPU

For profiling a single case:

```sh
nix develop --command perf record --call-graph dwarf -- ./build-nix/xcdf-perf-test --scenario vector_variable --events 1000000 --repetitions 1 --keep-files --output-dir /tmp
nix develop --command perf report
```
