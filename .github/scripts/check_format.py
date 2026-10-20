#!/usr/bin/env python3
"""Check tracked C++/Objective-C++ files on any CI OS without rewriting them."""
from pathlib import Path
import subprocess
import sys


def main():
    paths = subprocess.check_output(['git', 'ls-files', '-z', '--', 'src/', 'tests/']).split(b'\0')
    files = [p.decode() for p in paths if p and Path(p.decode()).suffix in {'.cpp', '.h', '.mm'}]
    if not files:
        print('No tracked C++ sources found', file=sys.stderr)
        return 1
    return subprocess.run(['clang-format', '--dry-run', '--Werror', *files]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
