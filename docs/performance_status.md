# XCDF performance status

Date: 2026-06-11

## Goal

Improve read-side XCDF performance for HAWC without changing the on-disk format.
The motivating workload is HAWC raw trigger data, especially on older CPUs used
in batch environments.

## Benchmark infrastructure added

- Nix development shell: `flake.nix`, `flake.lock`
- Structured performance harness under `tests/perf/`
- HAWC trigger fixture mode: `hawc_trig_fixture`
- Local slice generator: `bench/make_hawc_trig_slice.sh`
- CSV comparison helper: `bench/compare_perf.py`
- Performance guide: `docs/performance.md`

The benchmark CSV includes provenance metadata:

- XCDF version and git hash
- compiler and C++ version
- hostname, kernel/architecture, CPU model
- scenario, fixture path, event count, seek count, repetitions

## HAWC trigger file

Local full file used for slicing:

```text
trig_run005113_00001.xcd
size: ~2.4 GB
entries: 3,027,266
```

Representative schema includes:

```text
trig.eventID
trig.timeSliceID
trig.gpsSec
trig.gpsNanosec
trig.nHit4Edge
trig.nHit2Edge
trig.hitTime_4Edge
trig.loTOT_4Edge
trig.hiTOT_4Edge
trig.channelID_4Edge
trig.hitTime_2Edge
trig.loTOT_2Edge
trig.channelID_2Edge
```

Local benchmark slice:

```text
perf-fixtures/hawc_trig_500k.xcd
size: 380,220,261 bytes (~363 MiB)
events: 500,000
```

Slice generation command:

```sh
bench/make_hawc_trig_slice.sh trig_run005113_00001.xcd perf-fixtures/hawc_trig_500k.xcd 500000
```

## Baseline and candidate commands

Baseline/candidate benchmark command:

```sh
nix develop --command ./build-nix/xcdf-perf-test \
  --scenario hawc_trig_fixture \
  --fixture perf-fixtures/hawc_trig_500k.xcd \
  --events 500000 \
  --repetitions 3 \
  --seek-count 10000 \
  --csv
```

Comparison command:

```sh
bench/compare_perf.py perf-fixtures/hawc_trig_500k_baseline.csv perf-fixtures/hawc_trig_500k_combined_candidate.csv
```

## Current optimization changes

### 1. Fast intra-block seek

`XCDFFile::Seek()` previously decoded every event between the current position and
the requested event inside a block. For vector-heavy data, that means decoding all
child vectors for skipped events.

The candidate adds field-level `Skip()` support:

- vector parent/count fields are decoded, because child field lengths depend on
  them
- non-parent fields are skipped by advancing the bit pointer via
  `XCDFBlockData::SkipDatum()`
- the target event is decoded normally

This is a transparent read-path change and does not alter the file format.

### 2. Avoid active-max rediscovery for normal v3 reads

When usable trailer globals are already loaded, normal reads do not need to
rediscover active maxima for each datum. The candidate disables this per-datum
tracking for normal non-append v3 reads, while preserving it for paths that need
to recalculate globals.

This is also format-compatible.

## Measured result on local desktop

Machine metadata from benchmark CSV:

```text
hostname: enoch
uname: Linux 6.18.33 x86_64
cpu_model: AMD Ryzen 5 5600X 6-Core Processor
compiler: GCC 15.2.0
```

500k HAWC trigger fixture, median of 3 repetitions:

| Operation | Baseline seconds | Candidate seconds | Delta | Speedup |
| --- | ---: | ---: | ---: | ---: |
| read_full | 3.30442 | 3.18177 | -3.71% | 1.039x |
| read_scalars | 2.45958 | 2.18079 | -11.33% | 1.128x |
| seek | 39.0171 | 32.3664 | -17.05% | 1.205x |
| event_count | 0.000073332 | 0.000071782 | -2.11% | 1.022x |
| global_ranges | 0.000047521 | 0.000049701 | +4.59% | 0.956x |

Checksums matched for all operations.

Interpretation:

- The seek improvement is broad and expected: fewer skipped events are fully
  decoded.
- Sequential reads also improved, especially scalar-only reads, likely from
  avoiding unnecessary active-max tracking on normal v3 reads.
- `global_ranges` is already metadata/trailer dominated at tens of microseconds;
  the small apparent regression is not meaningful for the HAWC read goal.

## Correctness validation

CTest passed after the candidate changes from a clean test-data state:

```text
100% tests passed, 0 tests failed out of 8
```

Command:

```sh
rm -f build-nix/*.xcd appendtest.xcd seektest*.xcd randomtest.xcd speedtest.xcd buffertest.xcd
nix develop --command ctest --test-dir build-nix --output-on-failure
```

## Next recommended steps

1. Run the full synthetic/feature perf suite and compare broad regressions.
2. Run the 500k HAWC fixture with more repetitions if needed.
3. Profile `hawc_trig_fixture read_full` and `read_scalars` separately if further
   read improvements are desired.
4. Later, validate on the UMD batch CPU and/or the full 3M-event trigger file.

Potential future broad areas:

- Reduce extra copies between inflated frame buffers and block data.
- Consider an optional selective-read API for callers that only need a subset of
  fields. This would be a new API feature, not a transparent behavior change.
- Investigate zlib inflate/checksum costs on older CPUs.
