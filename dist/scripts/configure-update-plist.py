#!/usr/bin/env python3
"""Generate the same validated Info.plist for legacy qmake builds."""
import argparse
import base64
from pathlib import Path
import plistlib
from urllib.parse import urlparse
p = argparse.ArgumentParser()
p.add_argument('template'); p.add_argument('output'); p.add_argument('version')
a = p.parse_args()
import os
feed = os.environ.get('FOVELLE_UPDATE_FEED_URL', '')
key = os.environ.get('FOVELLE_UPDATE_PUBLIC_KEY', '')
if feed or key:
    url = urlparse(feed)
    if url.scheme != 'https' or not url.hostname or len(base64.b64decode(key, validate=True)) != 32:
        p.error('HTTPS feed and a 32-byte base64 Ed25519 public key are required together')
s = Path(a.template).read_text()
for name, value in dict(PROJECT_NAME='Fovelle', PROJECT_VERSION=a.version,
        MACOSX_ICON_FILENAME='qView.icns', CMAKE_OSX_DEPLOYMENT_TARGET='15.0',
        FOVELLE_UPDATE_FEED_URL=feed, FOVELLE_UPDATE_PUBLIC_KEY=key).items():
    s = s.replace('@'+name+'@', value)
plistlib.loads(s.encode()) # Catch malformed XML before qmake builds it.
Path(a.output).parent.mkdir(parents=True, exist_ok=True)
Path(a.output).write_text(s)
