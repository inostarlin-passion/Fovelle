#!/usr/bin/env python3
"""Run the final extracted app with Cocoa and its bundled EPS runtime, without developer overrides."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def smoke(app, output):
    app = Path(app).resolve()
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('QT_', 'DYLD_', 'GS_', 'FOVELLE_', 'APPLE_', 'SPARKLE_', 'GITHUB_', 'ACTIONS_'))}
    env.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin', QT_QPA_PLATFORM='cocoa',
               FOVELLE_DEPLOYMENT_PROBE='1', FOVELLE_DISABLE_AUTO_UPDATE_CHECK='1')
    executable = str(app / 'Contents/MacOS/Fovelle')
    records = []

    def run(name, command):
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=30)
        log = result.stdout + result.stderr
        (output / (name + '.log')).write_text(log)
        if result.returncode:
            raise ValueError(f'{name} failed ({result.returncode}): {log}')
        records.append({'name': name, 'returncode': result.returncode})
        return log

    run('startup', [executable, '--version'])
    gs = app / 'Contents/Resources/ghostscript/bin/gs'
    if run('ghostscript', [str(gs), '--version']).strip() != '10.07.1':
        raise ValueError('Final package must use the pinned Ghostscript 10.07.1 runtime')
    with tempfile.TemporaryDirectory(prefix='fovelle-artifact-smoke-') as directory:
        root = Path(directory)

        def chunk(kind, data):
            return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))

        (root / 'sample.png').write_bytes(b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('!2I5B', 2, 2, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(b'\x00' + b'\xff\x00\x00' * 2 + b'\x00' + b'\xff\x00\x00' * 2)) +
            chunk(b'IEND', b''))
        (root / 'sample.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="red"/></svg>')
        (root / 'sample.eps').write_text('%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 32 32\n1 0 0 setrgbcolor\n0 0 32 32 rectfill\nshowpage\n%%EOF\n')
        for extension in ('png', 'svg', 'eps'):
            env['FOVELLE_ARTIFACT_IMAGE'] = str(root / ('sample.' + extension))
            if 'FOVELLE_ARTIFACT_DECODE_OK' not in run(extension, [executable]):
                raise ValueError(f'{extension}: no successful real application decode was observed')
    (output / 'smoke.json').write_text(json.dumps({'passed': True, 'checks': records}, indent=2) + '\n')
    print('Final extracted app: Cocoa startup, PNG/SVG/EPS decode and pinned Ghostscript passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--output', type=Path, default=Path(os.environ.get(
        'FOVELLE_RELEASE_EVIDENCE_DIR', 'build/test-results/release-artifact')))
    args = parser.parse_args()
    smoke(args.app, args.output)
