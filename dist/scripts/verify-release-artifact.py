#!/usr/bin/env python3
"""Shared PR/release contract: every Mach-O slice, version and deployed dependency."""
import argparse
import importlib.util
import json
from pathlib import Path
import plistlib
import re
import subprocess

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('bundle_verifier', HERE / 'verify-macos-bundle.py')
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)
MAGIC = {bytes.fromhex(x) for x in ('feedface', 'cefaedfe', 'feedfacf', 'cffaedfe',
                                   'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca')}


def version_tuple(value):
    if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?', value):
        raise ValueError(f'Invalid Mach-O version: {value!r}')
    return tuple((list(map(int, value.split('.'))) + [0])[:3])


def validate_slice(load, *, main, minimum, sdk_major):
    # Older dependencies may use LC_VERSION_MIN_MACOSX instead of LC_BUILD_VERSION.
    match = re.search(r'^\s*minos\s+(\S+)', load, re.M)
    if match is None and 'LC_VERSION_MIN_MACOSX' in load:
        match = re.search(r'^\s*version\s+(\S+)', load, re.M)
    if match is None:
        raise ValueError('Mach-O slice has no minimum macOS version')
    actual = version_tuple(match[1])
    target = version_tuple(minimum)
    if actual > target or (main and actual != target):
        raise ValueError(f'Mach-O minimum {match[1]} is incompatible with {minimum}')
    sdk = re.search(r'^\s*sdk\s+(\S+)', load, re.M)
    if main and (sdk is None or sdk[1].split('.')[0] != sdk_major):
        raise ValueError(f'Main executable must use SDK family {sdk_major}')
    return {'minos': match[1], 'sdk': sdk[1] if sdk else None}


def verify(app, minimum='15.0', sdk_major='15', version=None):
    app = Path(app).resolve()
    bundle.verify(app)
    with (app / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    if info.get('LSMinimumSystemVersion') != minimum:
        raise ValueError('Info.plist LSMinimumSystemVersion mismatch')
    if version:
        for key in ('CFBundleVersion', 'CFBundleShortVersionString'):
            if info.get(key) != version:
                raise ValueError(f'Info.plist {key} mismatch')
    records = []
    main = app / 'Contents/MacOS/Fovelle'
    for path in sorted((app / 'Contents').rglob('*')):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open('rb') as stream:
            if stream.read(4) not in MAGIC:
                continue
        architectures = subprocess.check_output(['lipo', '-archs', str(path)], text=True).split()
        if not {'arm64', 'x86_64'} <= set(architectures):
            raise ValueError(f'non-Universal Mach-O: {path}: {architectures}')
        for arch in architectures:
            load = subprocess.check_output(['otool', '-arch', arch, '-l', str(path)], text=True)
            try:
                record = validate_slice(load, main=path == main, minimum=minimum, sdk_major=sdk_major)
            except ValueError as error:
                raise ValueError(f'{path} [{arch}]: {error}') from error
            records.append({'path': str(path.relative_to(app)), 'architecture': arch, **record})
    if not any(record['path'] == 'Contents/MacOS/Fovelle' for record in records):
        raise ValueError('No main Mach-O executable was validated')
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--minimum', default='15.0')
    parser.add_argument('--sdk-major', default='15')
    parser.add_argument('--version')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = {'passed': False}
    try:
        report['slices'] = verify(args.app, args.minimum, args.sdk_major, args.version)
        report['passed'] = True
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        report['error'] = str(error)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
