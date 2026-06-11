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
- HAWC MC reconstruction fixture mode: `hawc_mc_fixture`
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

## HAWC MC reconstruction files

Useful top-level MC reconstruction files are available under `/store/hawc/mc`.
On `thoth`, `/store` is HDD-backed, so benchmark fixtures should be copied into
ignored local storage before timing read/decode performance.

Current SSD-local MC fixture:

```text
perf-fixtures/hawc_mc_gamma.xcdf
source: /store/hawc/mc/gamma.xcdf
size: 1,435,452,798 bytes (~1.34 GiB)
entries: 947,175
```

The larger uncut validation target is:

```text
/store/hawc/mc/gamma_nocuts_aligned.xcdf
size: 27,385,860,717 bytes (~25.5 GiB)
entries: 20,974,295
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

## Measured result on thoth

Machine metadata from benchmark CSV:

```text
hostname: thoth
uname: Linux 6.18.33 x86_64
cpu_model: Intel(R) Core(TM) i9-9900K CPU @ 3.60GHz
compiler: GCC 15.2.0
```

Baseline was measured with the benchmark harness from the current checkout and
the library read-path sources restored to commit `9c60bb1`, the commit before the
recent performance work.

500k HAWC trigger fixture, median of 3 repetitions:

| Operation | Baseline seconds | Candidate seconds | Delta | Speedup |
| --- | ---: | ---: | ---: | ---: |
| read_full | 3.41006 | 2.82431 | -17.18% | 1.207x |
| read_scalars | 2.83729 | 2.34747 | -17.26% | 1.209x |
| seek | 44.3318 | 29.4844 | -33.49% | 1.504x |
| event_count | 0.000080898 | 0.000081947 | +1.30% | 0.987x |
| global_ranges | 0.000055086 | 0.000055671 | +1.06% | 0.989x |

250k HAWC MC reconstruction fixture, median of 3 repetitions:

| Operation | Baseline seconds | Candidate seconds | Delta | Speedup |
| --- | ---: | ---: | ---: | ---: |
| read_full | 3.95070 | 3.13846 | -20.56% | 1.259x |
| read_scalars | 3.11260 | 2.39643 | -23.01% | 1.299x |
| seek | 50.9338 | 33.9869 | -33.27% | 1.499x |
| event_count | 0.000149143 | 0.000151277 | +1.43% | 0.986x |
| global_ranges | 0.000126845 | 0.000124468 | -1.87% | 1.019x |

Checksums matched for all trigger and MC operations.

With async prefetch enabled and the normal 5k seek count, the current retained
candidate improves the same MC fixture further:

| Operation | Baseline seconds | Current seconds | Delta | Speedup |
| --- | ---: | ---: | ---: | ---: |
| read_full | 3.95070 | 2.00459 | -49.26% | 1.971x |
| read_scalars | 3.11260 | 1.93598 | -37.80% | 1.608x |
| seek | 50.9338 | 33.8567 | -33.53% | 1.504x |
| event_count | 0.000149143 | 0.000152766 | +2.43% | 0.976x |
| global_ranges | 0.000126845 | 0.000128969 | +1.67% | 0.984x |

Checksums matched.  This table uses
`perf-fixtures/hawc_mc_gamma_250k_prefetch_threshold_seek5000_candidate_thispc.csv`.

Synthetic/feature suite on `thoth`, 100k events, 3 repetitions, 10k seeks:

- Most serial read scenarios were faster or unchanged versus the same-host
  pre-performance baseline, including `speed_mix` read (-11.26%),
  `large_blocks` read (-33.77%), `vector_variable` read (-12.67%),
  `nested_vectors` read (-8.89%), `istream_read` (`read_file` -34.61%,
  `read_istream` -13.18%), `aliases` (`read_aliases` -19.99%), and
  `no_block_table` read (-2.16%).
- Seek-heavy synthetic scenarios generally improved, including `large_blocks`
  seek (-27.01%), `vector_variable` seek (-29.40%), `small_blocks` seek
  (-50.68%), and `speed_mix` seek (-24.65%).
- The only serial-read slowdowns above noise were small absolute timings:
  `scalar_ints` read changed from 0.00641 s to 0.00656 s (+2.30%) and
  `full_precision` read changed from 0.01219 s to 0.01267 s (+3.98%).
  `concatenated` read changed from 0.00201 s to 0.00214 s (+6.44%), also a
  sub-millisecond-scale absolute difference.

## Serial-read profile notes

A short `perf record` run on the local 100k-event MC reconstruction read showed
the remaining serial-read cost is dominated by decompression:

| Symbol / area | Approx. cycles |
| --- | ---: |
| zlib inflate (`inflate_fast`) | 46% |
| zlib Adler work | 6% |
| vector double decode | 17% |
| vector unsigned decode | 11% |
| benchmark-side full-read checksum/access | 7% |

Two attempted follow-up micro-optimizations were not retained:

- Filling known-length vectors by index instead of `Push()` regressed MC serial
  reads by about 5%.
- Inflating directly into the destination vector instead of a stack chunk plus
  `insert()` was neutral on both MC and trigger fixtures, so the copy is not the
  current limiting cost.
- Fast-loading scalar fields with zero active width helped the trigger fixture
  slightly but regressed the MC fixture (`read_full` +7.00%, `read_scalars`
  +2.71%), so it was not retained.
- An explicit selected-field `Read()` mode was prototyped to skip unrequested
  fields at the bit level.  On the local MC fixture, selected scalar reads were
  effectively tied with ordinary scalar reads (about 1.94 s for both), while the
  added API and default-path risk were not justified.  This does not rule out a
  deeper lazy per-event decode design for trigger workflows that first inspect a
  small count field and then decode full vectors only for passing events.

## zlib-ng experiment

The flake includes explicit zlib-ng zlib-compatible apps:

```sh
nix run .#test-zlib-ng
nix run .#perf-zlib-ng -- [xcdf-perf-test args]
```

The default zlib-ng build directory is `build-nix-zlib-ng`.  Concurrent zlib-ng
runs should set distinct `XCDF_BUILD_DIR` values; otherwise parallel Ninja
invocations can collide in the same build tree.

The zlib-ng build links to `zlib-ng-2.3.3` in zlib-compat mode and reads
existing zlib-written XCDF fixtures with matching checksums.  Initial
`random-test` runs failed in the repeated write/append path with a read failure
at byte offset 450.  The root cause was append-mode rewrite without truncation:
zlib-ng sometimes rewrote the final block/trailer one byte shorter than the
previous file contents, leaving stale trailing data.  `XCDFFile::Close()` now
truncates named append-mode files to the actual final trailer end position.

After that fix, both default zlib and zlib-ng CTest runs pass 8/8.

Benchmark results against existing fixtures, median of 3 repetitions, with
`--seek-count 0`:

Before async block prefetch:

| Fixture | Operation | Normal zlib seconds | zlib-ng seconds | Delta | Speedup |
| --- | --- | ---: | ---: | ---: | ---: |
| HAWC trigger 500k | read_full | 2.82431 | 2.56354 | -9.23% | 1.102x |
| HAWC trigger 500k | read_scalars | 2.34747 | 1.95313 | -16.80% | 1.202x |
| HAWC MC 250k | read_full | 3.13846 | 2.68060 | -14.59% | 1.171x |
| HAWC MC 250k | read_scalars | 2.39643 | 1.91609 | -20.04% | 1.251x |

Broad synthetic/feature suite with zlib-ng plus the current prefetch and
selected-read changes, 100k events, 3 repetitions, 10k seeks, compared with
normal zlib plus the same XCDF changes:

- All broad read paths improved, including `large_blocks` read (-14.57%),
  `vector_variable` read (-15.25%), `no_block_table` read (-11.68%),
  `istream_read` (`read_file` -13.82%, `read_istream` -14.90%),
  `concatenated` read (-7.18%), `full_precision` read (-24.93%), and
  `scalar_ints` read (-14.31%).
- Seek-heavy operations also improved, including `large_blocks` seek (-25.64%),
  `vector_variable` seek (-34.87%), `concatenated` seek (-16.73%),
  `many_fields` seek (-16.13%), and `speed_mix` seek (-19.23%).
- Full-scan compatibility paths also improved with zlib-ng:
  `no_block_table event_count_scan` (-31.44%) and `global_ranges_scan`
  (-18.51%).
- The only slower rows were writes in small/non-primary cases:
  `small_blocks` write (+5.38%) and `concatenated` write (+2.13%).
  Checksums matched.

## Async block prefetch experiment

Sequential reads of named disk files now prefetch one frame group ahead with a
background worker: after the current block data is loaded for event decoding,
the worker opens a separate input stream at the next byte offset and reads the
next frame; if that frame is a block header, it also reads and inflates the
paired block-data frame.  The main `Read()` path consumes that prefetched frame
group at the next block boundary and advances its own stream to the prefetched
end offset.

Current scope:

- Enabled for normal named disk-file reads.
- Disabled for stream-based input, append mode, and recover mode.
- Prefetch is launched only from sequential `Read()` block transitions.  Blocks
  loaded by `Seek()`, `Rewind()`, or metadata scans stay synchronous so random
  access does not spawn work that is immediately discarded.
- Prefetch starts only after a compressed block-data frame of at least 128 KiB,
  avoiding thread/file-open overhead on tiny synthetic block layouts.
- `XCDF_DISABLE_PREFETCH=1` disables the optimization for A/B benchmarking and
  debugging.

Median of 3 repetitions, `--seek-count 0`:

| Fixture | Backend | Operation | Before prefetch seconds | Prefetch seconds | Delta | Speedup |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| HAWC trigger 500k | zlib | read_full | 2.82431 | 2.18852 | -22.51% | 1.291x |
| HAWC trigger 500k | zlib | read_scalars | 2.34747 | 2.13538 | -9.03% | 1.099x |
| HAWC MC 250k | zlib | read_full | 3.13846 | 1.92687 | -38.60% | 1.629x |
| HAWC MC 250k | zlib | read_scalars | 2.39643 | 1.92378 | -19.72% | 1.246x |
| HAWC trigger 500k | zlib-ng | read_full | 2.56354 | 1.72363 | -32.76% | 1.487x |
| HAWC trigger 500k | zlib-ng | read_scalars | 1.95313 | 1.71193 | -12.35% | 1.141x |
| HAWC MC 250k | zlib-ng | read_full | 2.68060 | 1.59775 | -40.40% | 1.678x |
| HAWC MC 250k | zlib-ng | read_scalars | 1.91609 | 1.43427 | -25.15% | 1.336x |

One-repetition trigger validation with the normal 10k seek count showed seek
performance unchanged while serial reads still improved:

| Operation | Before prefetch seconds | Prefetch seconds | Delta | Speedup |
| --- | ---: | ---: | ---: | ---: |
| read_full | 2.82431 | 2.21087 | -21.72% | 1.277x |
| read_scalars | 2.34747 | 2.19187 | -6.63% | 1.071x |
| seek | 29.4844 | 29.5715 | +0.30% | 0.997x |

Same-binary A/B on the `/store` MC fixture
(`/store/hawc/mc/gamma.xcdf`), 250k events, 3 repetitions, `--seek-count 0`:

| Operation | Prefetch disabled seconds | Prefetch enabled seconds | Delta | Speedup |
| --- | ---: | ---: | ---: | ---: |
| read_full | 3.24871 | 2.02822 | -37.57% | 1.602x |
| read_scalars | 2.44853 | 2.00245 | -18.22% | 1.223x |

The `/store` A/B used the HDD-backed path but should be treated as a warm-cache
measurement because both runs read the same bounded fixture back-to-back.

Bounded-prefix validation on the larger uncut MC reconstruction file
(`/store/hawc/mc/gamma_nocuts_aligned.xcdf`), 250k of 20,974,295 events, 3
repetitions, `--seek-count 0`, normal zlib:

| Operation | Prefetch disabled seconds | Prefetch enabled seconds | Delta | Speedup |
| --- | ---: | ---: | ---: | ---: |
| read_full | 2.78869 | 1.82318 | -34.62% | 1.530x |
| read_scalars | 2.12853 | 1.80747 | -15.08% | 1.178x |

Same bounded prefix with zlib-ng:

| Operation | zlib-ng prefetch disabled seconds | zlib-ng prefetch enabled seconds | Delta | Speedup |
| --- | ---: | ---: | ---: | ---: |
| read_full | 2.44199 | 1.53412 | -37.18% | 1.592x |
| read_scalars | 1.72608 | 1.42245 | -17.59% | 1.213x |

Compared with normal zlib plus prefetch, zlib-ng plus prefetch improved the
same uncut prefix by 1.188x for `read_full` and 1.271x for `read_scalars`.
Compared with normal zlib and prefetch disabled, zlib-ng plus prefetch improved
`read_full` by 1.818x and `read_scalars` by 1.496x.

Checksums matched for both operations.  This is not a full 26 GB sweep; it
validates the real uncut reconstruction field layout and leading-event read path
without spending time on the whole file.

A longer bounded zlib-ng validation on the same uncut file, 1M of 20,974,295
events, confirmed the prefetch benefit at the larger prefix size:

| Operation | zlib-ng prefetch disabled seconds | zlib-ng prefetch enabled seconds | Delta | Speedup | Prefetch events/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| read_full | 9.19962 | 6.13621 | -33.30% | 1.499x | 162,967 |
| read_scalars | 6.72243 | 5.64626 | -16.01% | 1.191x | 177,109 |

The first 1M `read_full` repetition was a slower 15.33 s outlier, consistent
with HDD/cache state; the median of 3 repetitions is shown above.  Checksums
matched across the 1M disabled and enabled runs.

After adding the 128 KiB prefetch threshold, the broad synthetic/feature suite
returned to the expected shape:

- `small_blocks` read changed from 0.036185 s to 0.0362794 s (+0.26%, same),
  avoiding the large ungated-prefetch regression on tiny blocks.
- `scalar_ints` read changed from 0.00634715 s to 0.00636784 s (+0.33%, same).
- Large serial reads still benefit: `large_blocks` read improved from
  0.0228377 s to 0.0168544 s (-26.20%, 1.355x), and `istream_read`'s named-file
  path improved from 0.022899 s to 0.0169252 s (-26.09%, 1.353x).
- HAWC trigger 500k after the threshold remained faster than the pre-prefetch
  candidate: `read_full` 2.82431 s to 2.18852 s (-22.51%, 1.291x) and
  `read_scalars` 2.34747 s to 2.13538 s (-9.03%, 1.099x).

Current-tree same-binary A/B on the 500k trigger fixture, 3 repetitions,
`--seek-count 0`, after adding a hit-multiplicity-filtered benchmark operation:

| Operation | Prefetch disabled seconds | Prefetch enabled seconds | Delta | Speedup |
| --- | ---: | ---: | ---: | ---: |
| read_full | 2.80382 | 2.08171 | -25.75% | 1.347x |
| read_scalars | 2.33105 | 2.07312 | -11.06% | 1.124x |
| read_nhit_filtered | 2.48214 | 2.08372 | -16.05% | 1.191x |

The filtered operation checks `trig.nHit4Edge + trig.nHit2Edge >= 200` and only
touches full hit-vector contents for passing events.  On this fixture, 41,705 of
500,000 events pass the threshold (8.34%).  This benchmark does not yet avoid
materializing skipped event vectors inside `XCDFFile::Read()`; it provides a
target for a future lazy per-event decode API that would inspect predicate
fields first, then decode full vectors only for passing events.

That lazy per-event decode API is now prototyped as an opt-in selected read:
callers build an `XCDFFile::ReadSelector`, call `Read(selector)` to load only
predicate fields plus required vector parents, and call `ReloadCurrentEvent()`
to fully materialize the same event when the predicate passes.  The ordinary
`Read()` path is unchanged.

Current-tree result with default prefetch for ordinary `Read()` and synchronous
block loading for selected reads, 500k trigger fixture, 3 repetitions,
`--seek-count 0`:

| Operation | Median seconds | Notes |
| --- | ---: | --- |
| read_full | 2.13078 | Ordinary `Read()`, async prefetch |
| read_scalars | 2.12228 | Ordinary `Read()`, async prefetch |
| read_nhit_filtered | 2.13363 | Ordinary `Read()`, vectors already materialized |
| read_nhit_lazy | 1.84561 | Selected `Read(selector)` plus reload for passing events |

`read_nhit_lazy` matched the ordinary filtered checksum and was 1.156x faster
than ordinary filtered reads in the same run.  Against the same current tree with
prefetch disabled for all operations before the prefix-read benchmark fix, it was
1.329x faster than ordinary filtered reads (2.42675 s to 1.82650 s).  An earlier
run that let selected reads launch prefetch regressed `read_nhit_lazy` to about
2.06 s on the local fixture, so selected reads avoid starting background prefetch
by default; the cheaper selected decode gives less useful overlap and more
contention from the background inflate thread.

With zlib-ng, the same selected-read benchmark improved further:

| Operation | Normal zlib seconds | zlib-ng seconds | Speedup |
| --- | ---: | ---: | ---: |
| read_full | 2.13078 | 1.71407 | 1.243x |
| read_scalars | 2.12228 | 1.72611 | 1.230x |
| read_nhit_filtered | 2.13363 | 1.73504 | 1.230x |
| read_nhit_lazy | 1.84561 | 1.44120 | 1.281x |

For this trigger filter shape, zlib-ng plus selected reads gives 1.480x over
normal zlib ordinary filtered reads with prefetch (2.13363 s to 1.44120 s), and
1.684x over normal zlib ordinary filtered reads with prefetch disabled before the
prefix-read benchmark fix (2.42675 s to 1.44120 s).

Bounded validation on the larger HDD-backed trigger file
`/store/hawc/trig_run009099_00669.xcd`:

```text
size: 2,433,082,888 bytes
entries: 3,173,351
nHit4Edge + nHit2Edge >= 200: 266,909 events (8.41%)
```

This file has expensive metadata operations: `event_count` and `global_ranges`
scan the 2.4 GB file and take about 9-11 s on `thoth`.  The fixture benchmark
read operations were updated to consume the requested event prefix directly
instead of calling `GetEventCount()` first, so bounded prefix reads on files
without cheap trailer metadata are not polluted by a hidden full-file scan.

Corrected 1M-event prefix result, one repetition, normal zlib:

| Operation | Seconds | Notes |
| --- | ---: | --- |
| read_full | 3.63033 | Ordinary `Read()`, async prefetch |
| read_scalars | 3.50108 | Ordinary `Read()`, async prefetch |
| read_nhit_filtered | 3.55816 | Ordinary `Read()`, vectors already materialized |
| read_nhit_lazy | 3.92074 | Selected reads, synchronous block loading |
| read_nhit_lazy with `XCDF_PREFETCH_SELECTED_READS=1` | 3.47653 | Selected reads with prefetch |

On this HDD-backed prefix, selected reads need prefetch to avoid losing I/O
overlap.  With `XCDF_PREFETCH_SELECTED_READS=1`, lazy filtered reads matched the
ordinary filtered checksum and were 1.128x faster than synchronous selected
reads, 1.017x faster than ordinary filtered reads, and roughly tied with scalar
reads.  The default remains synchronous for selected reads because the local 500k
trigger fixture was decode-bound and selected-read prefetch erased the lazy
decode gain there.

The same 1M `/store` prefix with zlib-ng and
`XCDF_PREFETCH_SELECTED_READS=1`:

| Operation | Normal zlib seconds | zlib-ng seconds | Speedup |
| --- | ---: | ---: | ---: |
| read_full | 3.66294 | 3.43274 | 1.067x |
| read_scalars | 3.48026 | 2.70137 | 1.288x |
| read_nhit_filtered | 3.53472 | 2.69522 | 1.311x |
| read_nhit_lazy | 3.47653 | 2.65006 | 1.312x |
| event_count | 9.16255 | 6.58782 | 1.391x |
| global_ranges | 11.0618 | 8.77273 | 1.261x |

Checksums matched for the trigger read rows.  On this larger HDD-backed trigger
prefix, zlib-ng plus selected reads with prefetch gives 1.334x over normal zlib
ordinary filtered reads (3.53472 s to 2.65006 s).  The metadata scan rows also
improve because they must inflate/read the full file when cheap trailer metadata
is absent.

Compatibility spot-checks:

- A focused `concatenated` scenario run with 100k events and 10k seeks passed
  with stable checksums.  The prefetch path correctly advances the main stream
  after a prefetched trailer before probing for the next file header.
- Append and recover modes keep prefetch disabled.  Append remains covered by
  CTest, including the zlib-ng truncation fix.

## Correctness validation

CTest passed after the candidate changes from a clean test-data state:

```text
100% tests passed, 0 tests failed out of 8
```

Command:

```sh
nix run .#test
XCDF_BUILD_DIR=/tmp/xcdf-build-zlib-ng nix run .#test-zlib-ng
```

## Next recommended steps

1. Decide whether zlib-ng should remain an explicit Nix build option or become
   the preferred/default backend in this environment; it now has broad-suite,
   MC, local trigger, and HDD trigger evidence.
2. Decide whether selected-read prefetch should stay controlled by
   `XCDF_PREFETCH_SELECTED_READS=1` or become adaptive.  Local SSD-trigger
   validation favored synchronous selected reads; the HDD-backed trigger prefix
   favored selected-read prefetch.
3. Validate a longer bounded prefix of the uncut MC file only if another MC-side
   change is added.  The current uncut MC evidence already covers 250k and 1M
   prefixes without sweeping the full 26 GB file.
4. Later, validate on the UMD batch CPU and/or full production trigger workflow
   with the expected high-energy filter.

Potential future broad areas:

- Make selected-read prefetch adaptive if future validation identifies a stable
  signal for choosing between synchronous selected reads and background prefetch.
- Investigate zlib inflate/checksum costs on older CPUs.
