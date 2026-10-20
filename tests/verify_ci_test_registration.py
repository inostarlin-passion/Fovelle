#!/usr/bin/env python3
"""CI-T2: validate generated CTest properties, including optional matrices."""
import argparse
import json
from pathlib import Path
import subprocess


def verify(build):
    result = subprocess.run(['ctest', '--test-dir', str(build), '--show-only=json-v1'],
                            check=True, capture_output=True, text=True)
    registration = json.loads(result.stdout)
    tests = registration['tests']
    if not tests:
        raise ValueError(f'{build}: no registered tests')
    for test in tests:
        properties = {p['name']: p['value'] for p in test['properties']}
        if properties.get('TIMEOUT') != 0:
            raise ValueError(f'{test["name"]}: finite or inherited CTest timeout')
        if any(item.startswith('QTEST_FUNCTION_TIMEOUT=')
               for item in properties.get('ENVIRONMENT', [])):
            raise ValueError(f'{test["name"]}: custom QtTest watchdog')
    return {'build': str(build.resolve()), 'registered_tests': len(tests),
            'all_timeouts_zero': True, 'no_custom_qtest_watchdog': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builds', type=Path, nargs='+')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    records = [verify(build) for build in args.builds]
    text = json.dumps(records, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
