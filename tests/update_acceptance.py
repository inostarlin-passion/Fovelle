#!/usr/bin/env python3
"""Static contracts and real Sparkle standard UI/installer tests using disposable app bundles.
No production key, release, installed Fovelle bundle, or login keychain is modified.
"""
import argparse
import base64
import hashlib
import http.server
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
import zipfile
import struct

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
SPARKLE = BUILD / '_deps/sparkle'

class StaticTests(unittest.TestCase):
    def test_case_metadata_and_atomic_coverage(self):
        cases = json.loads((ROOT/'tests/update_test_cases.json').read_text())
        required = {'purpose', 'preconditions', 'input', 'steps', 'expected', 'postcondition'}
        self.assertEqual(len(cases), 16)
        self.assertEqual(len({c['id'] for c in cases}), 16)
        for criterion in range(1, 9):
            subset = [c for c in cases if c['acceptance'] == 'AC'+str(criterion)]
            self.assertEqual({c['kind'] for c in subset}, {'静态', '动态'})
        for case in cases:
            self.assertTrue(all(case.get(field) for field in required))
            for method in case['code']:
                if method.startswith('FeatureTests.'):
                    self.assertIn('void '+method.replace('.', '::')+'()', (ROOT/'tests/tst_qviewtests.cpp').read_text())
                else:
                    self.assertTrue(hasattr(StaticTests if method.startswith('StaticTests.') else DynamicTests, method.split('.')[-1]))

    def test_state_and_scheduler(self):
        source = (ROOT/'src/updatechecker_sparkle.mm').read_text()
        for token in ['canCheckForUpdates', 'removeObserver:', 'automaticallyChecksForUpdates',
                      'QV_DISABLE_ONLINE_VERSION_CHECK', 'FOVELLE_DISABLE_AUTO_UPDATE_CHECK',
                      'checkIntervalSeconds(frequency)', 'resetUpdateCycleAfterShortDelay']:
            self.assertIn(token, source)
        self.assertNotIn('checkedUpdates', (ROOT/'src/qvapplication.cpp').read_text())

    def test_release_configuration_rejects_invalid(self):
        for feed, key in [('', ''), ('http://example.com/appcast.xml', 'x'*43+'='),
                          ('https://example.com/appcast.xml', 'short')]:
            result = subprocess.run(['cmake', '-DFOVELLE_REQUIRE_UPDATE_CONFIG=ON',
                '-DFOVELLE_UPDATE_FEED_URL='+feed, '-DFOVELLE_UPDATE_PUBLIC_KEY='+key,
                '-P', str(ROOT/'cmake/Sparkle.cmake')], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Release updates require', result.stderr)

    def test_manual_entries_use_standard_ui(self):
        bridge = (ROOT/'src/updatechecker_sparkle.mm').read_text()
        self.assertIn('[impl->controller checkForUpdates:nil]', bridge)
        self.assertIn('initWithStartingUpdater:NO', bridge)
        self.assertNotIn('QDesktopServices', bridge)
        self.assertNotIn('api.github.com', (ROOT/'src/updatechecker.cpp').read_text())
        for source in ['src/actionmanager.cpp', 'src/qvaboutdialog.cpp']:
            self.assertIn('getUpdateChecker().check(true)', (ROOT/source).read_text())
        self.assertIn('setWindowModality(Qt::NonModal)', (ROOT/'src/qvaboutdialog.cpp').read_text())

    def test_secure_configuration_and_lifetime(self):
        bridge = (ROOT/'src/updatechecker_sparkle.mm').read_text()
        for token in ['isConfigurationValid(', 'removeObserver:', 'canCheckForUpdates', 'configurationError']:
            self.assertIn(token, bridge)
        plist = (ROOT/'dist/mac/Info.plist.in').read_text()
        for token in ['SUPublicEDKey', 'SUFeedURL', 'SUVerifyUpdateBeforeExtraction']:
            self.assertIn(token, plist)
        cmake = (ROOT/'cmake/Sparkle.cmake').read_text()
        self.assertIn('EXPECTED_HASH SHA256=', cmake)
        self.assertIn('/usr/bin/ditto', cmake)
        self.assertIn('@executable_path/../Frameworks', cmake)

    def test_release_closes_download_install_path(self):
        workflow = (ROOT/'.github/workflows/release.yml').read_text()
        for token in ['FOVELLE_REQUIRE_UPDATE_CONFIG', 'SPARKLE_PRIVATE_ED_KEY', 'build/update-release/appcast.xml']:
            self.assertIn(token, workflow)
        package = (ROOT/'dist/scripts/package-macos-release.sh').read_text()
        self.assertIn('-depth -type d', package)
        self.assertIn('--preserve-metadata=entitlements', package)
        self.assertLess(package.index('spctl --assess', package.index('VERIFIED_APP=')), package.index('bash "$RELEASE_ROOT/dist/scripts/generate-update-appcast.sh"'))
        for script in ['generate-update-appcast.sh', 'package-macos-release.sh']:
            subprocess.run(['bash', '-n', str(ROOT/'dist/scripts'/script)], check=True)

class DynamicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (BUILD/'tests/fovelle_sparkle_probe').exists():
            raise RuntimeError('Build fovelle_sparkle_probe before running dynamic tests')

    def run_scenario(self, mode, version='2.0.0', malformed=False, tampered=False):
        with tempfile.TemporaryDirectory(prefix='fovelle-sparkle-test-') as directory:
            work = Path(directory)
            events = work/'events.txt'; events.touch()
            bundle_identifier = 'io.github.inostarlin-passion.Fovelle.UpdateFixture.'+work.name
            # OpenSSL's DER encodings end with the raw 32-byte private seed/public key.
            openssl = shutil.which('openssl')
            subprocess.run([openssl, 'genpkey', '-algorithm', 'ED25519', '-out', str(work/'key.pem')], check=True, capture_output=True)
            private_der = subprocess.check_output([openssl, 'pkey', '-in', str(work/'key.pem'), '-outform', 'DER'])
            public_der = subprocess.check_output([openssl, 'pkey', '-in', str(work/'key.pem'), '-pubout', '-outform', 'DER'])
            key_file = work/'seed'; key_file.write_bytes(base64.b64encode(private_der[-32:]))
            key_file.chmod(0o600)
            public = base64.b64encode(public_der[-32:]).decode()
            observed = {'feed_requests': 0, 'downloads': 0, 'progress_before_feed_response': False}
            archive = work/'new.zip'
            feed = b''
            class Handler(http.server.BaseHTTPRequestHandler):
                def log_message(self, *args): pass
                def do_GET(self):
                    if self.path == '/appcast.xml':
                        observed['feed_requests'] += 1
                        time.sleep(1.2) # Native progress must be visible before feed returns.
                        observed['progress_before_feed_response'] = events.exists() and 'CHECK_PROGRESS_VISIBLE' in events.read_text()
                        payload = feed
                    else:
                        observed['downloads'] += 1
                        payload = archive.read_bytes()
                    self.send_response(503 if mode == 'network-error' else 200); self.send_header('Content-Length', str(len(payload))); self.end_headers()
                    try:
                        for offset in range(0, len(payload), 65536):
                            self.wfile.write(payload[offset:offset+65536]); self.wfile.flush()
                            if self.path != '/appcast.xml': time.sleep(0.05)
                    except (BrokenPipeError, ConnectionResetError): pass
            server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
            server.daemon_threads = True
            url = f'http://127.0.0.1:{server.server_port}'
            def make_app(parent, app_version):
                app = parent/'UpdateFixture.app'
                (app/'Contents/MacOS').mkdir(parents=True)
                shutil.copy2(BUILD/'tests/fovelle_sparkle_probe', app/'Contents/MacOS/UpdateFixture')
                subprocess.run(['ditto', str(SPARKLE/'Sparkle.framework'), str(app/'Contents/Frameworks/Sparkle.framework')], check=True)
                # Probe is Cocoa-only; it uses the same official controller entry point.
                plist = dict(CFBundleExecutable='UpdateFixture', CFBundleName='UpdateFixture',
                    CFBundleIdentifier=bundle_identifier, CFBundlePackageType='APPL',
                    CFBundleVersion=app_version, CFBundleShortVersionString=app_version,
                    SUFeedURL=url+'/appcast.xml', SUPublicEDKey=public,
                    SUEnableAutomaticChecks=False, SUAutomaticallyUpdate=False,
                    SUVerifyUpdateBeforeExtraction=True,
                    NSAppTransportSecurity={'NSAllowsLocalNetworking': True},
                    FovelleTestEvents=str(events))
                (app/'Contents/Info.plist').write_bytes(plistlib.dumps(plist))
                subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True, capture_output=True)
                return app
            newer = make_app(work/'new', '2.0.0')
            # Enough incompressible bytes to observe/cancel real download progress.
            (newer/'Contents/Resources').mkdir()
            (newer/'Contents/Resources/padding').write_bytes(os.urandom(2*1024*1024))
            subprocess.run(['codesign', '--force', '--sign', '-', str(newer)], check=True, capture_output=True)
            subprocess.run(['ditto', '-c', '-k', '--keepParent', str(newer), str(archive)], check=True)
            if mode == 'install':
                release_env = dict(os.environ, SPARKLE_PRIVATE_ED_KEY=key_file.read_text(),
                    RELEASE_TAG='v2.0.0', RELEASE_ZIP_PATH=str(archive), RELEASE_APP_PATH=str(newer),
                    FOVELLE_APPCAST_OUTPUT_DIR=str(work/'published'), FOVELLE_SPARKLE_ROOT=str(SPARKLE))
                subprocess.run(['bash', str(ROOT/'dist/scripts/generate-update-appcast.sh')],
                    env=release_env, check=True, capture_output=True)
                import xml.etree.ElementTree as ET
                enclosure = ET.parse(work/'published/appcast.xml').find('.//enclosure')
                self.assertEqual(enclosure.attrib['url'], 'https://github.com/inostarlin-passion/Fovelle/releases/download/v2.0.0/new.zip')
                self.assertIn('{http://www.andymatuschak.org/xml-namespaces/sparkle}edSignature', enclosure.attrib)
                generated_signature = enclosure.attrib['{http://www.andymatuschak.org/xml-namespaces/sparkle}edSignature']
                subprocess.run([str(SPARKLE/'bin/sign_update'), '--ed-key-file', str(key_file),
                    '--verify', str(archive), generated_signature], check=True, capture_output=True)
                self.assertEqual(ET.parse(work/'published/appcast.xml').find('.//{http://www.andymatuschak.org/xml-namespaces/sparkle}version').text, '2.0.0')
                mismatch_env = dict(release_env, SPARKLE_PRIVATE_ED_KEY=base64.b64encode(os.urandom(32)).decode())
                mismatch = subprocess.run(['bash', str(ROOT/'dist/scripts/generate-update-appcast.sh')], env=mismatch_env, capture_output=True)
                self.assertNotEqual(mismatch.returncode, 0)
            signature = subprocess.check_output([str(SPARKLE/'bin/sign_update'), '--ed-key-file', str(key_file), '-p', str(archive)], text=True).strip()
            if tampered:
                with zipfile.ZipFile(archive) as z:
                    entry = next(i for i in z.infolist() if i.filename.endswith('/padding'))
                data = bytearray(archive.read_bytes())
                filename_length, extra_length = struct.unpack_from('<HH', data, entry.header_offset+26)
                offset = entry.header_offset+30+filename_length+extra_length+100
                data[offset] ^= 0xff
                archive.write_bytes(data)
            feed = (f'''<?xml version="1.0"?><rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle"><channel><title>Fixture</title><item><title>Version {version}</title><sparkle:version>{version}</sparkle:version><enclosure url="{url}/new.zip" length="{archive.stat().st_size}" type="application/octet-stream" sparkle:edSignature="{signature}"/></item></channel></rss>''').encode()
            if malformed: feed = b'<invalid'
            older = make_app(work/'old', '1.0.0')
            original = hashlib.sha256((older/'Contents/Info.plist').read_bytes()).hexdigest()
            threading.Thread(target=server.serve_forever, daemon=True).start()
            env = dict(os.environ, FOVELLE_UPDATE_TEST_MODE=mode, FOVELLE_UPDATE_TEST_EVENTS=str(events))
            process = subprocess.Popen([str(older/'Contents/MacOS/UpdateFixture')], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic()+45
                while time.monotonic() < deadline:
                    recorded = events.read_text()
                    if mode == 'install' and 'RELAUNCHED_NEW_VERSION' in recorded: break
                    if mode != 'install' and process.poll() is not None: break
                    time.sleep(.1)
                recorded = events.read_text()
                self.assertIn('CHECK_PROGRESS_VISIBLE', recorded)
                self.assertEqual(observed['feed_requests'], 1)
                if mode != 'cancel-check': self.assertTrue(observed['progress_before_feed_response'])
                if mode == 'install':
                    self.assertIn('REPEATED_MANUAL_CHECK', recorded)
                    self.assertLess(recorded.index('CHECK_PROGRESS_VISIBLE'), recorded.index('UPDATE_OFFER_VISIBLE'))
                    for expected in ['UPDATE_OFFER_VISIBLE', 'DOWNLOAD_FINISHED', 'EXTRACTION_FINISHED', 'INSTALL_STARTED', 'RELAUNCHED_NEW_VERSION']:
                        self.assertIn(expected, recorded)
                    self.assertEqual(plistlib.loads((older/'Contents/Info.plist').read_bytes())['CFBundleVersion'], '2.0.0')
                else:
                    self.assertEqual(hashlib.sha256((older/'Contents/Info.plist').read_bytes()).hexdigest(), original)
                    self.assertNotIn('INSTALL_STARTED', recorded)
                    self.assertNotIn('RELAUNCHED_NEW_VERSION', recorded)
                    if mode.startswith('cancel-'):
                        self.assertIn('CANCEL_', recorded)
                        self.assertTrue('READY_AFTER_CANCEL' in recorded or 'CYCLE_READY' in recorded)
                    else:
                        self.assertIn('RESULT_ALERT_VISIBLE', recorded)
                        self.assertIn('CYCLE_READY', recorded)
                    if tampered:
                        self.assertIn('ERROR_', recorded)
                        self.assertIn('EdDSA signature does not match', recorded)
                    if malformed or mode == 'network-error': self.assertIn('ERROR_', recorded)
                    if version == '1.0.0': self.assertEqual(observed['downloads'], 0)
                print(json.dumps({'mode': mode, 'events': recorded.splitlines(), **observed}), flush=True)
            finally:
                if process.poll() is None: process.kill()
                # All these processes belong exclusively to the disposable fixture.
                for line in subprocess.check_output(['ps', '-axo', 'pid=,command='], text=True).splitlines():
                    if str(work) in line:
                        try: os.kill(int(line.strip().split(None, 1)[0]), 9)
                        except ProcessLookupError: pass
                stdout, stderr = process.communicate(timeout=5)
                server.shutdown(); server.server_close()
                if 'CYCLE_READY' not in recorded and 'READY_AFTER_CANCEL' not in recorded and 'RELAUNCHED_NEW_VERSION' not in recorded: print('PROBE_EXIT', process.returncode, stderr[-4000:])
                subprocess.run(['defaults', 'delete', bundle_identifier], capture_output=True)
                shutil.rmtree(Path.home()/'Library/Caches'/bundle_identifier, ignore_errors=True)

    def test_check_cancel(self): self.run_scenario('cancel-check')
    def test_download_cancel(self): self.run_scenario('cancel-download')
    def test_network_error(self): self.run_scenario('network-error')
    def test_no_update(self): self.run_scenario('no-update', version='1.0.0')
    def test_invalid_feed(self): self.run_scenario('invalid', malformed=True)
    def test_tampered_archive_rejected(self): self.run_scenario('tampered', tampered=True)
    def test_download_install_relaunch(self): self.run_scenario('install')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--static-only', action='store_true'); args = parser.parse_args()
    suite = unittest.TestLoader().loadTestsFromTestCase(StaticTests)
    if not args.static_only: suite.addTests(unittest.TestLoader().loadTestsFromTestCase(DynamicTests))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())
