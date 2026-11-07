#!/usr/bin/env python3
"""Run the Qt About regression with a disposable bundle and a stalled HTTPS feed."""

import os
from pathlib import Path
import plistlib
import shutil
import socket
import subprocess
import sys
import tempfile


def main(test_binary):
    with tempfile.TemporaryDirectory(prefix="fovelle-about-update-") as directory, \
            socket.create_server(("127.0.0.1", 0)) as feed:
        bundle_id = f"io.github.inostarlin-passion.Fovelle.AboutUpdate.{Path(directory).name}"
        app = Path(directory) / "AboutUpdateFixture.app"
        executable = app / "Contents/MacOS/AboutUpdateFixture"
        executable.parent.mkdir(parents=True)
        shutil.copy2(test_binary, executable)
        (app / "Contents/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleExecutable": executable.name,
            "CFBundleIdentifier": bundle_id,
            "CFBundleName": "AboutUpdateFixture",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": "1.0.0",
            "CFBundleVersion": "1.0.0",
            # TCP can connect, but TLS receives no reply before the test cancels.
            "SUFeedURL": f"https://127.0.0.1:{feed.getsockname()[1]}/appcast.xml",
            # Public key from the RFC 8032 Ed25519 test vectors.
            "SUPublicEDKey": "11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=",
            "SUEnableAutomaticChecks": False,
        }))
        subprocess.run(["codesign", "--force", "--sign", "-", str(app)], check=True)
        env = dict(os.environ, QT_QPA_PLATFORM="cocoa", FOVELLE_DISABLE_AUTO_UPDATE_CHECK="1",
                   FOVELLE_TEST_SUITE="AboutUpdateTests")
        try:
            return subprocess.run([str(executable), "-v1"], env=env, timeout=60).returncode
        finally:
            subprocess.run(["defaults", "delete", bundle_id], capture_output=True)
            shutil.rmtree(Path.home() / "Library/Caches" / bundle_id, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main(Path(sys.argv[1])))
