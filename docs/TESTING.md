# Testing Fovelle

Build once, then use the same default gate locally and in CI:

```sh
cmake -S . -B build -DBUILD_TESTS=ON -DFOVELLE_EXTENDED_TESTS=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The test command has a three-minute limit. Installing dependencies, compiling,
format/lint jobs, and packaging/signing are separate operations. The runner shares
one 165-second deadline across every stage; CTest allows 175 seconds for execution
and cleanup, and Actions limits the test step to three minutes. A timeout fails
the gate; it never counts as a successful partial run.

`tests/fast_test_manifest.json` defines the default coverage. Selected Qt functions
retain their original assertions. Functions/data rows are not run again through
separate overlapping CTest entries. GUI suites execute sequentially in isolated
processes to avoid focus interference. Local and CI profiles execute the same
manifest; `FOVELLE_TEST_PROFILE=ci` only identifies the evidence.

The default gate includes all ImageLoader format/color/vector performance tests,
core zoom/pan and settings checks, animation speed/pause/queued-read checks,
network and shutdown checks, representative native alerts and SDR full-screen
transitions, HDR pixel/math checks, and the mandatory real-file HDR gate. The HDR
gate retains both bundled fixtures and all ten full-screen continuity rows, with
strict XML verification requiring zero failures and zero skips.

Logs, XML and per-stage timings are written to `build/test-results/fast/` and
`build/test-results/portable-hdr/`. `fast/summary.json` records every planned stage,
including failures and stages not run. Missing/duplicate functions, unexpected
rows, skipped Qt cases and stale XML cannot produce a passing result.

For the original exhaustive matrix, explicitly opt into the longer gate:

```sh
cmake -S . -B build -DFOVELLE_EXTENDED_TESTS=ON
ctest --test-dir build --output-on-failure
# Restore the default after a deep regression run:
cmake -S . -B build -DFOVELLE_EXTENDED_TESTS=OFF
```

This includes the original full suites, all focused regressions, redundant state
combinations, longer fault injections and deployment smoke checks. It is outside
the default three-minute budget. External-fixture and Accessibility-dependent
checks still require their original explicit configuration and permissions.
Full-screen state/geometry combinations, all Info-dialog close paths, second
HiDPI passes and the delayed RAW injection remain in this deep matrix rather
than being deleted or silently skipped by the default gate.
