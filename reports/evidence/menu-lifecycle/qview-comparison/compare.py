#!/usr/bin/env python3
"""qView/Fovelle cross-project comparison, each retaining default window geometry.
Derived from tests/lifecycle_benchmark.py; removes Fovelle-only maximization requirement.
"baseline" denotes qView 7.1; "candidate" denotes current Fovelle 1.3.3.
"""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def run(executable, output):
    env = dict(os.environ, FOVELLE_DEPLOYMENT_PROBE='1', FOVELLE_SYSTEM_PROBE='1',
               FOVELLE_STARTUP_PERF='1', FOVELLE_DISABLE_AUTO_UPDATE_CHECK='1')
    start = time.perf_counter()
    process = subprocess.Popen([str(executable)], env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    import threading
    watchdog = threading.Timer(15, process.kill)
    watchdog.start()
    markers = {}
    lines = []
    try:
        for line in process.stdout:
            now = time.perf_counter()
            lines.append(line)
            for marker in ['phase=first-event-loop-turn', 'phase=menus-ready', 'phase=system-probe']:
                if marker in line:
                    markers[marker] = now
        code = process.wait(timeout=5)
        end = time.perf_counter()
    finally:
        watchdog.cancel()
        if process.poll() is None:
            process.kill()
            process.wait()
    output.write_text(''.join(lines))
    assert code == 0, f'{output}: exit {code}'
    assert len(markers) == 3, f'{output}: missing readiness/quit markers'
    ready = max(markers['phase=first-event-loop-turn'], markers['phase=menus-ready'])
    assert ready < markers['phase=system-probe'], 'Quit happened before ready'
    assert 'windows=1' in ''.join(lines), 'Expected exactly one window'
    if 'visible=' in ''.join(lines):
        assert 'visible=true' in ''.join(lines), 'qView window not visible'
    return dict(startup_ms=(ready-start)*1000,
                exit_ms=(end-markers['phase=system-probe'])*1000, returncode=code)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--runs', type=int, default=10)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.runs >= 10, 'At least ten measured pairs required'
    args.output.mkdir(parents=True, exist_ok=True)
    paths = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    samples = {key: [] for key in paths}
    for key, path in paths.items():
        run(path, args.output / f'{key}-warmup.log')
    for index in range(args.runs):
        for key in (['baseline', 'candidate'] if index % 2 == 0 else ['candidate', 'baseline']):
            samples[key].append(run(paths[key], args.output / f'{key}-{index:02}.log'))
    result = {'runs': samples, 'metric': 'parent monotonic wall clock; spawn to event-loop AND menus ready; pre-quit system-probe to full process exit', 'summary': {}}
    for metric in ['startup_ms', 'exit_ms']:
        before = statistics.median(s[metric] for s in samples['baseline'])
        after = statistics.median(s[metric] for s in samples['candidate'])
        result['summary'][metric] = dict(baseline_median=before, candidate_median=after,
                                        reduction_percent=100*(1-after/before), passed=after <= before*0.5)
    result['passed'] = all(v['passed'] for v in result['summary'].values())
    (args.output / 'benchmark.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result['summary'], indent=2))
    return 0 if result['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
