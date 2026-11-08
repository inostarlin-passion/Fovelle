#!/usr/bin/env python3
"""Compile the CMake navigation probe as a macOS Universal object."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path, help="CMake build with CMAKE_EXPORT_COMPILE_COMMANDS=ON")
    args = parser.parse_args()
    entries = json.loads((args.build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    entry = next(item for item in entries if Path(item["file"]).name == "native_titlebar_probe.mm")
    command = shlex.split(entry["command"])
    while "-arch" in command:
        index = command.index("-arch")
        del command[index : index + 2]
    architectures = ("x86_64", "arm64")
    command.extend(("-arch", architectures[0], "-arch", architectures[1], "-Werror=c++11-narrowing"))

    with tempfile.TemporaryDirectory(prefix="fovelle-probe-compile-") as temp_dir:
        output = Path(temp_dir) / "native_titlebar_probe.o"
        command[command.index("-o") + 1] = str(output)
        subprocess.run(command, cwd=entry["directory"], check=True)
        actual = subprocess.check_output(["lipo", "-archs", str(output)], text=True).split()
        if set(actual) != set(architectures):
            raise SystemExit(f"Expected {architectures}, found {actual}")
    print("native_titlebar_probe.mm compiled for x86_64 and arm64")


if __name__ == "__main__":
    main()
