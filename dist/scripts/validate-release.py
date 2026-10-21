#!/usr/bin/env python3
"""Reject unstable/mismatched release identities before building or publishing."""
import argparse
import os
from pathlib import Path
import plistlib
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SPARKLE = '{http://www.andymatuschak.org/xml-namespaces/sparkle}'


def validate(tag, version, app=None, appcast=None, archive=None):
    if not re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', version):
        raise ValueError('VERSION must be a stable MAJOR.MINOR.PATCH version')
    if tag != 'v' + version:
        raise ValueError(f'Release tag {tag!r} must equal v{version}; prereleases are not supported')
    if app:
        with (Path(app) / 'Contents/Info.plist').open('rb') as stream:
            info = plistlib.load(stream)
        for key in ('CFBundleVersion', 'CFBundleShortVersionString'):
            if info.get(key) != version:
                raise ValueError(f'{key} must equal {version}, observed {info.get(key)!r}')
    if appcast:
        items = ET.parse(appcast).findall('./channel/item')
        if len(items) != 1:
            raise ValueError('Release appcast must contain exactly one update')
        item = items[0]
        enclosure = item.find('enclosure')
        if item.findtext(SPARKLE + 'version') != version:
            raise ValueError('Appcast version does not match VERSION')
        if enclosure is None or not enclosure.get(SPARKLE + 'edSignature'):
            raise ValueError('Appcast must contain a signed enclosure')
        if not archive:
            raise ValueError('Archive is required to validate the appcast')
        repository = os.environ.get('GITHUB_REPOSITORY', 'inostarlin-passion/Fovelle')
        expected = f'https://github.com/{repository}/releases/download/{tag}/{Path(archive).name}'
        if enclosure.get('url') != expected or enclosure.get('length') != str(Path(archive).stat().st_size):
            raise ValueError('Appcast URL or archive length does not match the release artifact')
    return tag


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--version-file', type=Path, default=ROOT / 'VERSION')
    parser.add_argument('--app', type=Path)
    parser.add_argument('--appcast', type=Path)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    tag = validate(args.tag, args.version_file.read_text().strip(), args.app, args.appcast, args.archive)
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
            stream.write(f'tag={tag}\n')
    print(f'Stable release identity verified: {tag}')


if __name__ == '__main__':
    main()
