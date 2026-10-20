# Testing Fovelle

Build once, then use the same default gate locally and in CI:

```sh
cmake -S . -B build -DBUILD_TESTS=ON -DFOVELLE_EXTENDED_TESTS=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The test command has a one-minute limit. Installing dependencies, compiling,
format/lint jobs, and packaging/signing are separate operations. The runner shares
one 55-second deadline across every stage; CTest allows 58 seconds for execution
and cleanup, and Actions limits the test step to one minute. A timeout fails
the gate; it never counts as a successful partial run.

`tests/fast_test_manifest.json` defines the default coverage. Selected Qt functions
retain their original assertions. Functions/data rows are not run again through
separate overlapping CTest entries. GUI suites execute sequentially in isolated
processes to avoid focus interference. Local and CI profiles execute the same
manifest; `FOVELLE_TEST_PROFILE=ci` only identifies the evidence.

The default gate covers every existing risk domain: format/color loading, vector
performance, zoom/pan and settings, animation pause/queued-read/speed, downloads,
shutdown, native alerts/full-screen, HDR pixels/math and real HDR files. Assertions
and production timing remain unchanged. Expensive parameter matrices use these
representatives (the complete matrices remain available through extended tests):

| Area | Default representatives | Extended coverage |
| --- | --- | --- |
| Vector frame budget | SVG 2x, EPS 1x | Both formats at both scales |
| Persist zoom preference across processes | macOS native settings | INI settings as well |
| Keep zoom during navigation | PNG 1.2, SVG 64 | All formats and zoom levels |
| Scrollbar trajectory | Keyboard in, wheel out with expensive refinement | All input/direction/refinement combinations |
| Movie pause/resume | Synchronous GIF without cache, asynchronous APNG with cache | All eight combinations |
| Queued movie read | Completed, inflight, pending destruction | Every cancellation/replacement/jump path |
| Movie speed | Fast, delayed GUI dispatch | Slow and normal speed as well |
| HDR surface metadata | Gain map | RAW and delayed RAW as well |
| Native window | Standard alert buttons, visible raster round trip | Appearance/severity, hidden vector, refinement |
| HDR full-screen continuity | Visible exit bottom, hidden enter size, hidden exit position | All ten phase/visibility rows |

Both bundled HDR fixtures still execute through the real-file decode gate.
The default surface-metadata and full-screen tests use the gain-map JPEG to avoid
repeated RAW decoding on hosted runners; native color fidelity still executes.
The standalone HDR gate and extended surface-metadata tests retain the RAW path.
Every selected row must pass with zero skips. The standalone `python3 tests/run_portable_hdr.py build/tests/fovelle_tests` retains
all ten HDR full-screen rows. EPS movie probing, additional shortcut-editor
interactions and all Info-dialog close/restore paths remain in extended tests.
The runner timeout regression uses a one-second injected deadline; native test
waits are unchanged.

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
the default one-minute budget. External-fixture and Accessibility-dependent
checks still require their original explicit configuration and permissions.
Full-screen state/geometry combinations, all Info-dialog close paths, second
HiDPI passes and the delayed RAW injection remain in this deep matrix rather
than being deleted or silently skipped by the default gate.

The budget is for test execution after building, not for a cold CI job. Hosted
runner performance must be checked in Actions; the `ci` profile uses the same
commands locally but does not emulate hosted hardware.

Budget and selection behavior follow the official [Qt test
reference](https://doc.qt.io/qt-6/qtest-overview.html#qt-test-command-line-arguments), [CTest TIMEOUT
property](https://cmake.org/cmake/help/latest/prop_test/TIMEOUT.html), and
[Actions step timeout reference](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#jobsjob_idstepstimeout-minutes).
Timeouts fail the gate; they do not establish that the selected coverage can pass
within the target. Use the emitted summary and XML to verify actual completion.

Initial measurements on the development Mac on 2026-10-20, before hosted-runner
feedback (superseded by the selection changes above):

| Invocation | CTest wall time | Result |
| --- | --- | --- |
| Default local profile | 45.95 s | All 39 stages passed |
| CI profile with workflow command | 45.67 s | All 39 stages passed |

The previous saved gate summary took 96.60 s. These measurements confirm local
completion below one minute; the CI-profile measurement is local, and hosted
Actions subsequently failed at the 55-second shared deadline. Both failed runs
had no assertion failures before timeout. Their XML identified EPS 2x rendering,
repeated RAW decoding and the second settings backend as excessive default work.
The revised selection above retains both vector formats/scales, native settings
restart checks and real RAW decoding, while leaving the additional combinations
in the extended gate. The one-minute CI limit and all assertions remain intact.
