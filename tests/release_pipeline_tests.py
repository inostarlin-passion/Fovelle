#!/usr/bin/env python3
"""Failure-path regressions for release identities, architecture slices and workflow gates."""
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

import yaml

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT / 'dist/scripts' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


identity = load('validate-release')
contract = load('verify-release-artifact')


class ReleaseTests(unittest.TestCase):
    def test_unstable_and_mismatched_tags_are_rejected(self):
        for tag in ('v1.3.5', 'v1.3.4-rc.1', 'version', 'v.1.3.4', '1.3.4'):
            with self.assertRaises(ValueError):
                identity.validate(tag, '1.3.4')
        self.assertEqual(identity.validate('v1.3.4', '1.3.4'), 'v1.3.4')

    def test_app_and_feed_must_match_the_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / 'Fovelle.app'
            (app / 'Contents').mkdir(parents=True)
            info = app / 'Contents/Info.plist'
            info.write_bytes(plistlib.dumps({'CFBundleVersion': '1.3.3', 'CFBundleShortVersionString': '1.3.4'}))
            with self.assertRaisesRegex(ValueError, 'CFBundleVersion'):
                identity.validate('v1.3.4', '1.3.4', app=app)
            info.write_bytes(plistlib.dumps({'CFBundleVersion': '1.3.4', 'CFBundleShortVersionString': '1.3.4'}))
            archive = root / 'Fovelle.zip'
            archive.write_bytes(b'archive fixture')
            feed = root / 'appcast.xml'
            text = '<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle"><channel><item><sparkle:version>1.3.4</sparkle:version><enclosure url="https://github.com/inostarlin-passion/Fovelle/releases/download/v1.3.4/Fovelle.zip" length="15" sparkle:edSignature="fixture"/></item></channel></rss>'
            feed.write_text(text)
            with mock.patch.dict(os.environ, {'GITHUB_REPOSITORY': 'inostarlin-passion/Fovelle'}):
                identity.validate('v1.3.4', '1.3.4', app, feed, archive)
                for bad in (text.replace('>1.3.4<', '>1.3.3<'), text.replace('length="15"', 'length="1"'),
                            text.replace('/v1.3.4/', '/v1.3.3/'), text.replace('sparkle:edSignature="fixture"', '')):
                    feed.write_text(bad)
                    with self.assertRaises(ValueError):
                        identity.validate('v1.3.4', '1.3.4', app, feed, archive)

    def test_per_slice_versions_and_sdk_are_fail_closed(self):
        good = 'cmd LC_BUILD_VERSION\n minos 15.0\n sdk 15.5\n'
        contract.validate_slice(good, main=True, minimum='15.0', sdk_major='15')
        contract.validate_slice('cmd LC_VERSION_MIN_MACOSX\n version 10.13\n sdk 14.0',
                                main=False, minimum='15.0', sdk_major='15')
        for bad in (good.replace('15.0', '15.0.1'), good.replace('15.0', '26.0'),
                    good.replace('15.5', '26.2'), 'cmd LC_BUILD_VERSION'):
            with self.assertRaises(ValueError):
                contract.validate_slice(bad, main=True, minimum='15.0', sdk_major='15')

    @unittest.skipUnless(shutil.which('xcrun') and shutil.which('lipo'), 'Apple toolchain required')
    def test_real_universal_file_cannot_hide_an_incompatible_second_slice(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'sample.c'
            source.write_text('int sample(void) { return 0; }\n')
            objects = []
            for arch, minimum in [('x86_64', '15.0'), ('arm64', '26.0')]:
                output = root / (arch + '.o')
                subprocess.run(['xcrun', 'clang', '-arch', arch, '-mmacosx-version-min=' + minimum,
                                '-c', str(source), '-o', str(output)], check=True, capture_output=True)
                objects.append(str(output))
            fat = root / 'universal.o'
            subprocess.run(['lipo', '-create', *objects, '-output', str(fat)], check=True)
            for arch, compatible in [('x86_64', True), ('arm64', False)]:
                text = subprocess.check_output(['otool', '-arch', arch, '-l', str(fat)], text=True)
                if compatible:
                    contract.validate_slice(text, main=False, minimum='15.0', sdk_major='15')
                else:
                    with self.assertRaises(ValueError):
                        contract.validate_slice(text, main=False, minimum='15.0', sdk_major='15')

            # Exercise the complete file iterator, mocking only unrelated Qt deployment checks.
            app = root / 'Fovelle.app'
            (app / 'Contents/MacOS').mkdir(parents=True)
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({'LSMinimumSystemVersion': '15.0'}))
            shutil.copy2(fat, app / 'Contents/MacOS/Fovelle')
            sdk_major = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-version'], text=True).split('.')[0]
            with mock.patch.object(contract.bundle, 'verify'):
                with self.assertRaisesRegex(ValueError, r'\[arm64\]'):
                    contract.verify(app, sdk_major=sdk_major)
                subprocess.run(['xcrun', 'clang', '-arch', 'arm64', '-mmacosx-version-min=15.0',
                                '-c', str(source), '-o', str(root / 'arm64.o')], check=True, capture_output=True)
                subprocess.run(['lipo', '-create', *objects, '-output', str(app / 'Contents/MacOS/Fovelle')], check=True)
                records = contract.verify(app, sdk_major=sdk_major)
                self.assertEqual({r['architecture'] for r in records}, {'x86_64', 'arm64'})
                shutil.copy2(root / 'x86_64.o', app / 'Contents/MacOS/Fovelle')
                with self.assertRaisesRegex(ValueError, 'non-Universal'):
                    contract.verify(app, sdk_major=sdk_major)

    @unittest.skipUnless(shutil.which('clang-tidy'), 'clang-tidy required')
    def test_tidy_warning_blocks_cpp_and_objcpp(self):
        source = ('namespace std { template<class T> T&& move(T& v) { return static_cast<T&&>(v); } }\n'
                  'struct Value { Value(); Value(const Value&); };\nvoid take(Value);\n'
                  'void f(const Value& v) { take(std::move(v)); }\n')
        with tempfile.TemporaryDirectory() as directory:
            for extension in ('cpp', 'mm'):
                path = Path(directory) / ('example.' + extension)
                path.write_text(source)
                result = subprocess.run(['clang-tidy', str(path), '--config-file=' + str(ROOT / '.clang-tidy'),
                                         '--', '-std=c++17'], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('performance-move-const-arg', result.stdout + result.stderr)

    def test_unsigned_mode_cannot_sign_a_production_update(self):
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(('RELEASE_', 'FOVELLE_', 'APPLE_', 'SPARKLE_'))}
        env.update(RELEASE_UNSIGNED='true', FOVELLE_GENERATE_APPCAST='true')
        result = subprocess.run(['bash', str(ROOT / 'dist/scripts/package-macos-release.sh')],
                                env=env, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsigned compatibility packages cannot generate a release appcast', result.stderr)

    def test_release_workflow_closes_all_gates(self):
        def workflow(name):
            return yaml.load((ROOT / '.github/workflows' / name).read_text(), Loader=yaml.BaseLoader)
        release = workflow('release.yml')
        self.assertEqual(release['jobs']['ci']['uses'], './.github/workflows/build.yml')
        self.assertEqual(release['jobs']['release']['needs'], 'ci')
        build = workflow('build.yml')
        self.assertIn('workflow_call', build['on'])
        self.assertNotEqual(build['concurrency']['group'], release['concurrency']['group'])
        config = next(s for s in build['jobs']['build']['steps'] if s.get('name') == 'Configure')['run']
        self.assertIn('CMAKE_OBJCXX_CLANG_TIDY=clang-tidy', config)
        steps = release['jobs']['release']['steps']
        package = next(s for s in steps if s.get('name') == 'Sign, notarize, validate, and package Fovelle')
        self.assertNotIn('if', package)  # manual runs must sign and notarize too
        publish = next(s for s in steps if s.get('uses', '').startswith('softprops/'))
        self.assertEqual(publish['if'], "github.event_name == 'push'")
        self.assertEqual(publish['with']['prerelease'], 'false')
        self.assertTrue(any('validate-release.py' in s.get('run', '') for s in steps))
        config = next(s for s in steps if s.get('name') == 'Configure')['run']
        self.assertIn('FOVELLE_ENABLE_UPDATE_INSTALL_TESTS=ON', config)
        compat = workflow('release-compatibility.yml')['jobs']['release-sdk15-compatibility']['steps']
        self.assertTrue(any('package-macos-release.sh' in s.get('run', '') and
                            s.get('env', {}).get('RELEASE_UNSIGNED') == 'true' for s in compat))
        package = (ROOT / 'dist/scripts/package-macos-release.sh').read_text()
        self.assertGreater(package.index('smoke-release-artifact.py', package.index('TEMP_ROOT="${RUNNER_TEMP')),
                           package.index('VERIFIED_APP="$VERIFY_ROOT/$(basename'))
        cmake = (ROOT / 'tests/CMakeLists.txt').read_text()
        self.assertGreater(cmake.index('add_test(NAME FovelleUpdateInstall'), cmake.index('# Release acceptance is independent'))


if __name__ == '__main__':
    unittest.main()
