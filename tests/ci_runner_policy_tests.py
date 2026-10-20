#!/usr/bin/env python3
"""CI topology, pinned toolchains, failure paths and hosted capacity regressions."""
import copy
import importlib.util
import json
import os
import tempfile
from pathlib import Path
import re
import subprocess
import unittest
from unittest import mock

import yaml

ROOT = Path(__file__).resolve().parents[1]
FILES = ['build.yml', 'test.yml', 'release.yml', 'release-compatibility.yml']
MAC_JOBS = {'build.yml': ('build', 'main'), 'release.yml': ('release', 'compatibility'),
            'release-compatibility.yml': ('release-sdk15-compatibility', 'compatibility')}


def load_workflows():
    return {name: yaml.load((ROOT / '.github/workflows' / name).read_text(), Loader=yaml.BaseLoader)
            for name in FILES}


def validate_labels(workflows, policy):
    for name, (job, lane) in MAC_JOBS.items():
        if workflows[name]['jobs'][job].get('runs-on') != policy['lanes'][lane]['runner']:
            raise ValueError(f'{name}: unexpected hosted runner pool')
    if workflows['test.yml']['jobs']['format'].get('runs-on') != policy['lanes']['format']['runner']:
        raise ValueError('Formatting must use the pinned Linux runner')


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / '.github/scripts' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RunnerPolicyTests(unittest.TestCase):
    def setUp(self):
        self.workflows = load_workflows()
        self.policy = json.loads((ROOT / '.github/ci-environment.json').read_text())

    def test_hosted_pool_labels_and_toolchain_roles(self):
        validate_labels(self.workflows, self.policy)
        for name, (job_name, lane) in MAC_JOBS.items():
            job = self.workflows[name]['jobs'][job_name]
            self.assertEqual(job['env']['DEVELOPER_DIR'], self.policy['lanes'][lane]['developer_dir'])
            self.assertEqual(job['env']['MACOSX_DEPLOYMENT_TARGET'], self.policy['deployment_target'])
            steps = job['steps']
            python = next(s for s in steps if s.get('uses', '').startswith('actions/setup-python@'))
            qt = next(s for s in steps if s.get('uses', '').startswith('jurplel/install-qt-action@'))
            self.assertEqual(python['with']['python-version'], self.policy['python'])
            self.assertEqual(qt['with']['version'], self.policy['qt'])
            self.assertEqual(qt['with']['setup-python'], 'false')
            self.assertEqual(qt['with']['aqtversion'], '==' + self.policy['aqtinstall'])
            self.assertEqual(qt['with']['py7zrversion'], '==' + self.policy['py7zr'])
            text = '\n'.join(s.get('run', '') for s in steps)
            self.assertIn('verify_environment.py --lane ' + lane, text)
            self.assertIn('Python3_EXECUTABLE="$(command -v python)"', text)
            self.assertNotIn('.venv/bin/python', text)
            self.assertIn('-DCMAKE_BUILD_TYPE=Release', text)
            self.assertIn('-G Ninja', text)
            self.assertIn('-DCMAKE_OSX_SYSROOT=', text)

    def test_all_ci_hosts_are_explicitly_arm_and_main_binary_is_arm64(self):
        expected = {'main': ('macos-15', 'arm64'),
                    'compatibility': ('macos-15', 'arm64'),
                    'format': ('ubuntu-24.04-arm', 'aarch64')}
        for lane, (label, architecture) in expected.items():
            self.assertEqual(self.policy['lanes'][lane]['runner'], label)
            self.assertEqual(self.policy['lanes'][lane]['architecture'], architecture)
        build = self.workflows['build.yml']['jobs']['build']
        commands = '\n'.join(step.get('run', '') for step in build['steps'])
        self.assertIn('-DCMAKE_OSX_ARCHITECTURES=arm64', commands)

    def test_actual_mac_arm_guards_reject_intel(self):
        guards = []
        for name, (job, _) in MAC_JOBS.items():
            commands = '\n'.join(s.get('run', '') for s in self.workflows[name]['jobs'][job]['steps'])
            matches = [line.strip() for line in commands.splitlines() if 'test "$(uname -m)"' in line]
            self.assertEqual(matches, ['test "$(uname -m)" = "arm64"'])
            guards.extend(matches)
        with tempfile.TemporaryDirectory() as directory:
            uname = Path(directory) / 'uname'
            env = dict(os.environ, PATH=directory + os.pathsep + os.environ['PATH'])
            for architecture, code in [('arm64', 0), ('x86_64', 1)]:
                uname.write_text('#!/bin/sh\nprintf "%s\\n" "' + architecture + '"\n')
                uname.chmod(0o755)
                for guard in guards:
                    result = subprocess.run(['bash', '-c', guard], env=env, capture_output=True)
                    self.assertEqual(result.returncode, code)

    def test_one_mac_build_and_reusable_linux_preflight_per_push(self):
        build, checks = self.workflows['build.yml'], self.workflows['test.yml']
        self.assertEqual(set(build['jobs']), {'format', 'build'})
        self.assertEqual(build['jobs']['format']['uses'], './.github/workflows/test.yml')
        self.assertEqual(build['jobs']['build']['needs'], 'format')
        self.assertEqual(set(checks['on']), {'workflow_call', 'workflow_dispatch'})
        self.assertEqual(set(checks['jobs']), {'format'})
        self.assertEqual(set(build['on']), {'push', 'pull_request', 'workflow_dispatch'})
        mac = build['jobs']['build']['steps']
        self.assertEqual(sum('cmake --build build' in s.get('run', '') for s in mac), 1)
        self.assertEqual(sum('ctest --test-dir build' in s.get('run', '') for s in mac), 1)
        commands = '\n'.join(s.get('run', '') for s in mac)
        self.assertIn('-DCMAKE_CXX_CLANG_TIDY=clang-tidy', commands)
        self.assertNotIn('build.sh --tidy', commands)
        fmt = checks['jobs']['format']['steps']
        self.assertFalse(any('install-qt' in s.get('uses', '') for s in fmt))
        self.assertTrue(any('check_format.py' in s.get('run', '') for s in fmt))
        self.assertTrue(any('ci_runner_policy_tests.py' in s.get('run', '') for s in fmt))

    def test_packages_actions_permissions_and_diagnostics_are_pinned(self):
        for file, workflow in self.workflows.items():
            self.assertEqual(workflow['permissions'], {'contents': 'read'})
            self.assertNotIn('pull_request_target', workflow['on'])
            for job in workflow['jobs'].values():
                for step in job.get('steps', []):
                    uses = step.get('uses', '')
                    if uses:
                        self.assertRegex(uses, r'^[\w.-]+/[\w./-]+@[0-9a-f]{40}$')
                    if uses.startswith('actions/checkout@'):
                        self.assertEqual(step['with']['persist-credentials'], 'false')
                    if uses.startswith('actions/setup-python@'):
                        self.assertEqual(step['with']['python-version'], self.policy['python'])
                        self.assertEqual(step['with']['cache'], 'pip')
                    if 'permissions' in job:
                        self.assertEqual(file, 'release.yml')
                        self.assertEqual(job['permissions'], {'contents': 'write'})
                if job.get('steps'):
                    uploads = [s for s in job['steps'] if s.get('uses', '').startswith('actions/upload-artifact@')]
                    self.assertTrue(any(s.get('if') == 'always()' for s in uploads), file)
        self.assertEqual(self.workflows['release.yml']['jobs']['release']['permissions'], {'contents': 'write'})
        requirements = (ROOT / '.github/requirements-ci.txt').read_text()
        for package, key in [('cmake', 'cmake'), ('ninja', 'ninja'), ('clang-tidy', 'llvm')]:
            self.assertIn(package + '==' + self.policy[key], requirements)
        self.assertIn('clang-format==' + self.policy['llvm'], (ROOT / '.github/requirements-format.txt').read_text())
        dependabot = yaml.load((ROOT / '.github/dependabot.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual({u['package-ecosystem'] for u in dependabot['updates']}, {'github-actions', 'pip'})

    def test_concurrency_and_unlimited_test_policy_are_preserved(self):
        for file in ['build.yml', 'release-compatibility.yml']:
            group = self.workflows[file]['concurrency']
            self.assertEqual(group['cancel-in-progress'], 'true')
            self.assertIn('github.workflow', group['group'])
            self.assertIn('github.event.pull_request.number || github.ref', group['group'])
        self.assertNotIn('concurrency', self.workflows['test.yml'])  # reusable caller owns its group
        self.assertEqual(self.workflows['release.yml']['concurrency']['cancel-in-progress'], 'false')
        for workflow in self.workflows.values():
            for job in workflow['jobs'].values():
                self.assertNotIn('timeout-minutes', job)
                for step in job.get('steps', []):
                    if 'ctest ' in step.get('run', ''):
                        self.assertNotIn('timeout-minutes', step)
                        self.assertNotIn('--timeout', step['run'])
        manifest = json.loads((ROOT / 'tests/fast_test_manifest.json').read_text())
        self.assertEqual(sum(e['script'] == Path(__file__).name for e in manifest['static']), 1)
        self.assertNotIn('budget_seconds', manifest)

    def test_release_universal_and_compatibility_contracts_remain(self):
        for file in ['release.yml', 'release-compatibility.yml']:
            text = (ROOT / '.github/workflows' / file).read_text()
            self.assertIn('-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"', text)
            self.assertIn('-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0', text)
            self.assertIn('test "${SDK_VERSION%%.*}" -eq 15', text)
        release = self.workflows['release.yml']['jobs']['release']['steps']
        for step in release:
            if 'package-macos-release.sh' in step.get('run', '') or step.get('uses', '').startswith('softprops/'):
                self.assertEqual(step.get('if'), "github.event_name == 'push'")

    def test_wrong_runner_labels_are_rejected(self):
        for file, (job, _) in MAC_JOBS.items():
            for label in ['macos-26-intel', 'macos-15-intel', 'macos-latest', 'macos-26-large', ['self-hosted', 'macOS']]:
                workflows = copy.deepcopy(self.workflows)
                workflows[file]['jobs'][job]['runs-on'] = label
                with self.assertRaises(ValueError):
                    validate_labels(workflows, self.policy)

    def test_environment_accepts_each_lane_and_rejects_version_drift(self):
        env = load_script('verify_environment')
        for lane, target in self.policy['lanes'].items():
            observed = {k: v for k, v in target.items() if k != 'runner'}
            observed['python'] = self.policy['python']
            if lane == 'format':
                observed['clang_format'] = self.policy['llvm']
            else:
                observed.update({k: self.policy[k] for k in ['qt', 'cmake', 'ninja', 'deployment_target']})
                observed['clang_tidy'] = self.policy['llvm']
            self.assertEqual(env.validate(lane, observed, self.policy), [])
            for key in observed:
                changed = dict(observed, **{key: 'unexpected'})
                self.assertTrue(env.validate(lane, changed, self.policy), (lane, key))
        self.assertEqual(env.version('Xcode 26.6\nBuild version 17F113'), '26.6')
        with self.assertRaises(ValueError):
            env.version('not installed')

    def test_format_runner_preserves_names_coverage_and_failure_status(self):
        fmt = load_script('check_format')
        with mock.patch.object(fmt.subprocess, 'check_output', return_value=b'src/a name.cpp\0src/b.mm\0tests/a.h\0src/a.txt\0'), \
             mock.patch.object(fmt.subprocess, 'run', return_value=mock.Mock(returncode=7)) as run:
            self.assertEqual(fmt.main(), 7)
            self.assertEqual(run.call_args.args[0], ['clang-format', '--dry-run', '--Werror',
                                                    'src/a name.cpp', 'src/b.mm', 'tests/a.h'])
        with mock.patch.object(fmt.subprocess, 'check_output', return_value=b''):
            self.assertEqual(fmt.main(), 1)

    def test_build_script_propagates_format_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            for command, body in {'uname': 'echo Darwin', 'xcrun': 'echo /sdk',
                                  'python3': 'exit 7', 'clang-format': 'exit 7'}.items():
                file = Path(directory) / command
                file.write_text('#!/bin/sh\n' + body + '\n')
                file.chmod(0o755)
            env = dict(os.environ, PATH=directory + os.pathsep + os.environ['PATH'])
            result = subprocess.run(['bash', str(ROOT / 'build.sh'), '--format-check'],
                                    env=env, capture_output=True)
            self.assertEqual(result.returncode, 7)

    def test_environment_failure_still_writes_diagnostics(self):
        env = load_script('verify_environment')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'environment.json'
            argv = ['verify', '--lane', 'main', '--output', str(output)]
            with mock.patch.object(env.sys, 'argv', argv), \
                 mock.patch.object(env, 'collect', side_effect=FileNotFoundError('missing clang-tidy')):
                self.assertEqual(env.main(), 1)
            report = json.loads(output.read_text())
            self.assertEqual(report['lane'], 'main')
            self.assertIn('missing clang-tidy', report['errors'][0])


if __name__ == '__main__':
    unittest.main()
