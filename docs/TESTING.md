# Testing Fovelle

Build once, then run the full default test selection locally:

```sh
cmake -S . -B build -DBUILD_TESTS=ON -DFOVELLE_EXTENDED_TESTS=OFF
cmake --build build --parallel 2
/usr/bin/time -p ctest --test-dir build --output-on-failure
```

CTest and the default runner impose no execution-time budget (`TIMEOUT 0`).
Functional timing assertions, explicit subprocess timeouts and QtTest's own
watchdog remain active. A failure never counts as a successful partial run.
The local target is under one minute for the CTest run. Installing dependencies,
compiling, format/lint and packaging are separate. CI also enables the separate
Sparkle installation tests where configured.

`tests/fast_test_manifest.json` defines the default coverage. Selected Qt functions
retain their original assertions. The default lifecycle cases run alongside the other manifest selections.
The selected thumbnail cases run in their own Cocoa process. Source-only scripts run with up to four
workers and retain ordered per-stage evidence, including failures. GUI suites execute sequentially in isolated
processes to avoid focus interference. Local and CI profiles execute the same
manifest; `FOVELLE_TEST_PROFILE=ci` only identifies the evidence.

The default core gate spans all ten Qt suites: format/color loading, vector
behavior, zoom/pan and settings, animation pause/queued-read/speed, downloads,
shutdown, native alerts/full-screen, HDR pixels/math and real HDR files. Assertions
and production timing remain unchanged. Expensive parameter matrices use these
representatives (the complete matrices remain available through extended tests):

| Area | Default representatives | Extended coverage |
| --- | --- | --- |
| Vector behavior | Display target switch and request burst/destruction | Full SVG/EPS frame-budget benchmarks at 1x/2x |
| Startup preference migration | Preserve disabled navigation reset | Fresh and enabled profiles as well |
| Persist zoom preference across processes | macOS native settings | INI settings as well |
| Keep zoom during navigation | SVG 64 | PNG/XPM and other zoom levels |
| Scrollbar trajectory | Wheel out with expensive refinement | Other input/direction/refinement combinations |
| Movie pause/resume | Asynchronous APNG with cache | All eight combinations |
| Queued movie read | Inflight | Other cancellation/replacement/jump/destruction paths |
| Movie speed | 200% speed with delayed GUI dispatch | Slow, normal and fast without GUI delay as well |
| HDR surface metadata | Gain map | RAW and delayed RAW as well |
| Native window | Standard alert buttons, ready menus and one artwork direction per rendering path | Other directions, SDR round trips, appearance/severity, hidden vector and refinement |
| HDR full-screen continuity | Visible exit bottom and hidden enter size | All ten phase/visibility rows |
| Lifecycle | Normal Open With teardown, latest-request handling and application shutdown | Slow startup, platform latency and Open With shutdown subprocess |
| Thumbnails | Keyboard, wheel, transition, cache supersession, and JPEG/WebP/PNG/RAW decode representatives | Full browsing, presentation and decoder matrix |
| EPS | Valid rendering, diagnostic/file limits and cancellation | Real 30-second renderer timeout |

Both bundled HDR fixtures still execute through the real-file decode gate.
The default surface-metadata and full-screen tests use the gain-map JPEG to avoid
repeated RAW decoding on hosted runners; native color fidelity still executes.
The standalone HDR gate and extended surface-metadata tests retain the RAW path.
Every selected row must pass with zero skips. The standalone
`python3 tests/run_portable_hdr.py build/tests/fovelle_tests` retains all ten HDR
full-screen rows. EPS movie probing, additional shortcut-editor
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
combinations, longer fault injections and deployment smoke checks. It extends the default selection. External-fixture and Accessibility-dependent
checks still require their original explicit configuration and permissions.
Full-screen state/geometry combinations, all Info-dialog close paths, second
HiDPI passes and the delayed RAW injection remain in this deep matrix rather
than being deleted or silently skipped by the default gate.

The `ci` profile executes the same commands locally but does not emulate
hosted hardware. Inspect summary JSON and XML for actual completion.

Single-image HDR surface and full-screen cases copy their input into a temporary
folder before opening it. This prevents automatic adjacent-image preloading from
starting an unrelated RAW decode that can outlive the case and trigger the real
application shutdown exit code 75. Navigation/preload behavior keeps its own
regressions; production shutdown limits are unchanged.

Clipboard representation tests also copy their GIF and HDR inputs into the
test-owned folder. Source-lease tests wait for their decoder's read marker and
temporary directory removal instead of calling the application-wide shutdown
drain. A deliberately blocked unrelated worker verifies that canceled loader
ownership can be checked while other work is still running, without setting the
global shutdown flag and canceling the next test's image loads.

Shortcut width stability waits for the selected category animation and queued
Cocoa metrics pass before taking its baseline. This removes dependence on the
category left by preceding tests; the before/after width, persistence and edited
shortcut assertions remain unchanged.

The core set retains loader cache/error/cancellation, EPS decoding, AVIF
orientation, vector display-switch/burst/destruction and SVG/Netpbm color.
Downloads retain failure, total timeout, size limits and cancellation; movies
retain bounded cache, pause, inflight reads, delayed 200% playback and source
replacement; shutdown remains covered. All source-only scripts in the manifest and all three
mandatory HDR stages execute in the default gate.

Format/color permutations, camera presentation, secondary shortcut interactions,
zoom terminal-state permutations, additional movie/download scenarios, full
vector performance benchmarks and separate SDR full-screen round trips are in
the extended gate. The default HDR continuity rows still exercise native
enter/exit transitions, including pixel, size and position checks. The expanded matrix remains available for deeper format and interaction checks.

Menu state regressions are part of the default gate. For an isolated run:

```sh
FOVELLE_TEST_SUITE=WindowBehaviorTests QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 build/tests/fovelle_tests testLateMenuState testLateFullscreenMenu testLatePauseMenu -v1
python3 tests/menu_lifecycle_static.py
```

`tests/lifecycle_benchmark.py BASELINE_EXECUTABLE CANDIDATE_EXECUTABLE --output DIRECTORY`
compares real Cocoa processes, warming both builds once and alternating ten
measured pairs. Freeze a baseline from the original Release build before
rebuilding the candidate, keeping each executable's bundle resources/frameworks
available. The comparison includes process startup through event-loop and menu
readiness, and pre-quit through complete process termination. Both median
reductions must reach 50%; a missed target returns 1 and retains all observations.
Run this separately from GUI regressions or builds to avoid competing work.
The default correctness gate does not imply that this performance target passed.

The 2026-11-05 review regressions run by default in WindowBehaviorTests and
`review_fixes_static.py`. They cover deletion target identity during navigation,
EPS file and diagnostic limits, full-resolution clipboard pixels, and downloaded
source lifetimes across decoder cancellation, native images, MIME and Open With.
The separate extended EPS test waits for the production 30-second timeout.
After enabling extended tests, run it directly with:

```sh
ctest --test-dir build -R '^FovelleEPSRendererTimeout$' --output-on-failure
```
The six-part case specifications and completion evidence are documented in
`reports/test_case_specification.md` and `reports/test_completion_report.md`.
