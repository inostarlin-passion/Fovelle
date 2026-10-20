#!/usr/bin/env python3
"""P1: paired warm-cache process benchmark for the lifecycle fix."""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import statistics
from lifecycle_benchmark import run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    paths = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    samples = {key: [] for key in paths}
    for key, path in paths.items():
        run(path, args.output / f'{key}-warmup.log')
    for i in range(10):
        for key in (['baseline', 'candidate'] if i % 2 == 0 else ['candidate', 'baseline']):
            samples[key].append(run(paths[key], args.output / f'{key}-{i:02}.log'))
    summary = {}
    for metric in ['startup_ms', 'exit_ms']:
        before, after = [statistics.median(s[metric] for s in samples[key]) for key in paths]
        summary[metric] = dict(baseline_median=before, candidate_median=after,
                               reduction_percent=100 * (1-after/before),
                               passed=after < before if metric == 'startup_ms' else after <= before+20)
    result = dict(environment=platform.platform(),
                  sha256={key: hashlib.sha256(path.read_bytes()).hexdigest() for key,path in paths.items()},
                  metric='parent monotonic; spawn to first event-loop AND menus ready; quit probe to full exit',
                  limitations='warm cache, isolated INI, no image, one maximized Cocoa window; not optical first frame',
                  runs=samples, summary=summary, passed=all(s['passed'] for s in summary.values()))
    (args.output/'benchmark.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(summary, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
