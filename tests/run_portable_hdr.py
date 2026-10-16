#!/usr/bin/env python3
"""Mandatory real-file HDR gate; absent fixtures/results/skipped cases fail."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parent
FIXTURES=ROOT/'data/portable-hdr'
EXPECTED={'gain-map.jpg','bayer.dng'}

def validate_fixtures(root=FIXTURES):
    manifest=json.loads((root/'manifest.json').read_text())
    if set(manifest)!=EXPECTED: raise ValueError('Incomplete fixture manifest')
    for name,digest in manifest.items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:
            raise ValueError('Fixture checksum mismatch: '+name)

def validate_xml(path,expected):
    root=ET.parse(path).getroot()
    names={f.get('name') for f in root.findall('TestFunction')}
    if not set(expected)<=names: raise ValueError('Missing executed test functions')
    for test in root.findall('TestFunction'):
        incidents=test.findall('Incident')
        if not incidents or any(i.get('type')!='pass' for i in incidents):
            raise ValueError('Non-pass or skipped test: '+str(test.get('name')))

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('executable');parser.add_argument('--output',type=Path);args=parser.parse_args()
    validate_fixtures()
    env=os.environ.copy();env.update(QT_QPA_PLATFORM='cocoa',FOVELLE_DISABLE_AUTO_UPDATE_CHECK='1',FOVELLE_HDR_JPEG_SAMPLE=str(FIXTURES/'gain-map.jpg'),FOVELLE_HDR_FULLSCREEN_IMAGE=str(FIXTURES/'bayer.dng'))
    runs=[('RemediationTests',['testPortableHDR']),('HDRPolicyTests',['testHDRBrightnessNativeColorFidelity']),('WindowBehaviorTests',['testHDRFullScreenVisualContinuity'])]
    with tempfile.TemporaryDirectory(prefix='fovelle-hdr-gate-') as temporary:
        output=args.output or Path(temporary)
        output.mkdir(parents=True,exist_ok=True)
        for suite,functions in runs:
            xml=output/(suite+'.xml');env['FOVELLE_TEST_SUITE']=suite
            subprocess.run([args.executable,*functions,'-o',str(xml)+',xml'],env=env,check=True,timeout=180)
            validate_xml(xml,functions)
            print(suite+': actual HDR tests executed; zero failures/skips',flush=True)
if __name__=='__main__':main()
