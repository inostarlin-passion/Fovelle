#!/usr/bin/env python3
"""Explicit reverse test; restores production source and rebuilds in finally.

Run alone, never concurrently with builds or GUI tests:
python3 tests/actions_speed_mutation.py --build-dir build
"""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    source = ROOT / 'src/qvmovie.cpp'
    original = source.read_bytes()
    expression = b'return int( (qint64(delay) * qint64(100) ) / qint64(speed) );'
    if original.count(expression) != 1:
        raise SystemExit('Production scaling expression changed; review mutation before running')
    environment = dict(os.environ, QT_QPA_PLATFORM='cocoa', FOVELLE_TEST_SUITE='RemediationTests')
    try:
        source.write_bytes(original.replace(expression, b'return delay; // deliberate test mutation'))
        subprocess.run(['cmake', '--build', str(build), '--parallel', '2'], check=True, timeout=300)
        result = subprocess.run([str(build / 'tests/fovelle_tests'),
                                 'testMovieAsyncPlaybackSpeed:fast'], env=environment,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=30)
        print(result.stdout)
        if result.returncode == 0 or 'movie.nextFrameDelay()' not in result.stdout:
            raise RuntimeError('Speed regression was not detected by the exact frame-delay assertion')
        print('MUTATION_DETECTED: missing production speed scaling rejected')
    finally:
        source.write_bytes(original)
        subprocess.run(['cmake', '--build', str(build), '--parallel', '2'], check=True, timeout=300)
        if source.read_bytes() != original:
            raise RuntimeError('Production source restoration failed')
        print('PRODUCTION_SOURCE_RESTORED')


if __name__ == '__main__':
    main()
