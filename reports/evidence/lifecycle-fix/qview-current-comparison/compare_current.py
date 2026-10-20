#!/usr/bin/env python3
"""Current Fovelle vs upstream qView stable; paired, warm-cache process timings.
No speed target is imposed: nonzero return or missing readiness fails validity.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import threading
import time

MARKERS = ['phase=first-event-loop-turn', 'phase=menus-ready', 'phase=system-probe']


def run(executable, output, maximized, qview):
    env = {k: v for k, v in os.environ.items() if not k.startswith('FOVELLE_')}
    env.update(FOVELLE_DEPLOYMENT_PROBE='1', FOVELLE_SYSTEM_PROBE='1',
               FOVELLE_STARTUP_PERF='1', FOVELLE_DISABLE_AUTO_UPDATE_CHECK='1',
               QT_QPA_PLATFORM='cocoa')
    if maximized:
        env['FOVELLE_BENCH_MAXIMIZED'] = '1'
    start = time.perf_counter_ns()
    process = subprocess.Popen([str(executable)], env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    watchdog = threading.Timer(15, process.kill)
    watchdog.start()
    markers, lines = {}, []
    try:
        for line in process.stdout:
            now = time.perf_counter_ns()
            lines.append(line)
            for marker in MARKERS:
                if marker in line:
                    assert marker not in markers, f'duplicate {marker}'
                    markers[marker] = now
        code = process.wait(timeout=5)
        end = time.perf_counter_ns()
    finally:
        watchdog.cancel()
        if process.poll() is None:
            process.kill()
            process.wait()
        output.write_text(''.join(lines))
    assert code == 0, f'{output}: exit {code}'
    assert len(markers) == 3, f'{output}: missing readiness/quit markers'
    ready = max(markers[MARKERS[0]], markers[MARKERS[1]])
    assert ready < markers[MARKERS[2]], 'quit before readiness'
    log = ''.join(lines)
    assert 'windows=1' in log
    expected = maximized or not qview
    assert f'maximized={str(expected).lower()}' in log
    if qview:
        assert 'visible=true' in log
    return dict(startup_ms=(ready-start)/1e6, exit_ms=(end-markers[MARKERS[2]])/1e6,
                returncode=code)


def describe(values):
    q = statistics.quantiles(values, n=4)
    return dict(n=len(values), median_ms=statistics.median(values), mean_ms=statistics.mean(values),
                min_ms=min(values), max_ms=max(values), q1_ms=q[0], q3_ms=q[2])


def bootstrap_difference(a, b):
    # Resample matched pair indices; conditional uncertainty for these samples.
    rng = random.Random(20261020)
    n = len(a)
    diffs = []
    for _ in range(10000):
        indices = rng.choices(range(n), k=n)
        diffs.append(statistics.median(b[i] for i in indices)
                     - statistics.median(a[i] for i in indices))
    diffs.sort()
    return [diffs[249], diffs[9749]]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('qview', type=Path)
    parser.add_argument('fovelle', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    paths = {'qview': args.qview.resolve(), 'fovelle': args.fovelle.resolve()}
    args.output.mkdir(parents=True, exist_ok=True)
    result = dict(date='2026-10-20 Asia/Shanghai', platform=platform.platform(),
                  executable_sha256={k: hashlib.sha256(p.read_bytes()).hexdigest() for k,p in paths.items()},
                  qt='6.11.2', optimization='Release, arm64, C++17, deployment target 15.0',
                  metric='parent monotonic: spawn to first-event-loop AND menus-ready; pre-quit probe receipt to full process exit',
                  scope='new process, warm cache, fresh isolated INI, no image, updates disabled, direct binary launch',
                  difference='Fovelle minus qView; negative means Fovelle uses less time', batches={})
    for name, maximized in [('matched-maximized', True), ('app-defaults', False)]:
        output = args.output/name
        output.mkdir(exist_ok=True)
        samples = {k: [] for k in paths}
        for k,p in paths.items():
            run(p, output/f'{k}-warmup.log', maximized, k=='qview')
        for i in range(20):
            for k in (['qview','fovelle'] if i%2==0 else ['fovelle','qview']):
                samples[k].append(run(paths[k], output/f'{k}-{i:02}.log', maximized, k=='qview'))
        summary = {}
        for metric in ['startup_ms','exit_ms']:
            a,b = [[s[metric] for s in samples[k]] for k in paths]
            before,after = statistics.median(a),statistics.median(b)
            summary[metric] = dict(qview=describe(a), fovelle=describe(b),
                                   fovelle_minus_qview_ms=after-before,
                                   fovelle_reduction_percent=100*(1-after/before),
                                   paired_bootstrap_median_difference_95_ms=bootstrap_difference(a,b))
        result['batches'][name] = dict(runs=samples,summary=summary)
        (args.output/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
        print(name, json.dumps(summary,indent=2), flush=True)
    result['valid']=True
    (args.output/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
