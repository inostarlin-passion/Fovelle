#!/usr/bin/env python3
"""R9: deploy, validate, zip/extract, and start the actual copied executable."""
from pathlib import Path
import argparse
import importlib.util
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bundle_verifier',ROOT/'dist/scripts/verify-macos-bundle.py')
verifier=importlib.util.module_from_spec(spec);spec.loader.exec_module(verifier)

def run(app):
    with tempfile.TemporaryDirectory(prefix='fovelle-deployment-') as temporary:
        root=Path(temporary);deployed=root/'stage/Fovelle.app';deployed.parent.mkdir()
        subprocess.run(['ditto',str(app),str(deployed)],check=True)
        subprocess.run(['macdeployqt',str(deployed),'-always-overwrite'],check=True,timeout=120)
        verifier.verify(deployed,sanitize=True)
        verifier.sign_adhoc(deployed)
        archive=root/'artifact.zip'
        subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(deployed),str(archive)],check=True,timeout=120)
        extracted=root/'unpacked';extracted.mkdir()
        subprocess.run(['ditto','-x','-k',str(archive),str(extracted)],check=True,timeout=120)
        bundle=extracted/'Fovelle.app'
        verifier.verify(bundle)
        subprocess.run(['codesign','--verify','--deep','--strict',str(bundle)],check=True,timeout=60)
        env=os.environ.copy();env.update(FOVELLE_DEPLOYMENT_PROBE='1',FOVELLE_DISABLE_AUTO_UPDATE_CHECK='1',QT_QPA_PLATFORM='cocoa')
        # Remove developer Qt lookup overrides. Do not change HOME or user preferences.
        for name in ('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','DYLD_LIBRARY_PATH','DYLD_FRAMEWORK_PATH'):
            env.pop(name,None)
        result=subprocess.run([str(bundle/'Contents/MacOS/Fovelle'),'--version'],env=env,capture_output=True,text=True,check=True,timeout=20)
        if 'Fovelle' not in result.stdout: raise ValueError('The extracted executable did not report its version')
        print(result.stdout.strip())
        ghostscript=bundle/'Contents/Resources/ghostscript/bin/gs'
        if ghostscript.exists():
            subprocess.run([str(ghostscript),'--version'],env=env,check=True,timeout=20)
        # Prove missing platform plugins are rejected by the same verifier.
        (bundle/'Contents/PlugIns/platforms/libqcocoa.dylib').unlink()
        try: verifier.verify(bundle)
        except ValueError: pass
        else: raise ValueError('Missing Cocoa plugin was incorrectly accepted')
        print('Deployed/extracted executable started; missing-plugin counterexample rejected')
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('app',type=Path);args=parser.parse_args();run(args.app.resolve())
