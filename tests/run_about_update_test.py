#!/usr/bin/env python3
"""Run the Qt About regression with a disposable bundle and a stalled HTTPS feed."""

from contextlib import contextmanager
import os
from pathlib import Path
import plistlib
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading


@contextmanager
def stalled_feed(directory):
    connections = Path(directory) / "tls-connections"
    connections.touch()
    finished = threading.Event()

    class FeedHandler(socketserver.BaseRequestHandler):
        def handle(self):
            self.request.settimeout(5)
            try:
                if self.request.recv(1) != b"\x16":  # TLS handshake record
                    return
            except (TimeoutError, ConnectionError):
                return
            # Record only connections on which the client started TLS. Leave
            # the server silent so cancellation cannot race a feed response.
            with connections.open("ab") as signals:
                signals.write(b"\n")
            finished.wait()

    with socketserver.ThreadingTCPServer(("127.0.0.1", 0), FeedHandler) as server:
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            yield server.server_address[1], connections
        finally:
            finished.set()
            server.shutdown()
            thread.join()


def main(test_binary, test_arguments=()):
    with tempfile.TemporaryDirectory(prefix="fovelle-about-update-") as directory, \
            stalled_feed(directory) as (feed_port, connections):
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
            "SUFeedURL": f"https://127.0.0.1:{feed_port}/appcast.xml",
            # Public key from the RFC 8032 Ed25519 test vectors.
            "SUPublicEDKey": "11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=",
            "SUEnableAutomaticChecks": False,
        }))
        subprocess.run(["codesign", "--force", "--sign", "-", str(app)], check=True)
        env = dict(os.environ, QT_QPA_PLATFORM="cocoa", FOVELLE_DISABLE_AUTO_UPDATE_CHECK="1",
                   FOVELLE_TEST_SUITE="AboutUpdateTests",
                   FOVELLE_ABOUT_UPDATE_CONNECTIONS=str(connections))
        try:
            return subprocess.run([str(executable), "-v1", *test_arguments], env=env,
                                  timeout=60).returncode
        finally:
            subprocess.run(["defaults", "delete", bundle_id], capture_output=True)
            shutil.rmtree(Path.home() / "Library/Caches" / bundle_id, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main(Path(sys.argv[1]), sys.argv[2:]))
