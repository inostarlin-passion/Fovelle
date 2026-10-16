#!/usr/bin/env python3
"""Retry only the observed Apple ticket-service connection-loss failure."""
import argparse
import re
import subprocess
import time


RETRY_DELAYS = (10, 20, 40, 60)
COMMAND_TIMEOUT = 120
CONNECTION_LOST = re.compile(r'\bNSURLErrorDomain\s+Code=-1005\b')


def run_stapler(action: str, path: str) -> int:
    attempts = len(RETRY_DELAYS) + 1
    for attempt in range(1, attempts + 1):
        print(f'stapler {action}: attempt {attempt}/{attempts}', flush=True)
        try:
            result = subprocess.run(
                ['xcrun', 'stapler', action, path],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors='replace',
                timeout=COMMAND_TIMEOUT,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f'stapler {action} exceeded {COMMAND_TIMEOUT}s; refusing to publish', flush=True)
            return 124
        except OSError as error:
            print(f'Unable to execute stapler: {error}', flush=True)
            return 127
        output = result.stdout or ''
        print(output, end='' if output.endswith('\n') else '\n', flush=True)
        if result.returncode == 0:
            return 0
        # EX_NOHOST (68) alone can also mean an unnotarized app. Require the
        # exact Foundation network diagnostic observed in the failed release.
        if result.returncode != 68 or not CONNECTION_LOST.search(output):
            return result.returncode
        if attempt == attempts:
            print('Apple ticket-service connection retries exhausted; refusing to publish', flush=True)
            return result.returncode
        delay = RETRY_DELAYS[attempt - 1]
        print(f'Apple ticket-service connection lost; retrying in {delay}s', flush=True)
        time.sleep(delay)
    raise AssertionError('unreachable')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('staple', 'validate'))
    parser.add_argument('path')
    args = parser.parse_args()
    return run_stapler(args.action, args.path)


if __name__ == '__main__':
    raise SystemExit(main())
