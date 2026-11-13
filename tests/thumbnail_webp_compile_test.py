#!/usr/bin/env python3
"""Compile both thumbnail consumers without Homebrew's generic header fallback."""

import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def cocoa_compile_commands(build_dir):
    entries = json.loads((build_dir / "compile_commands.json").read_text())
    for target in ("Fovelle", "fovelle_tests"):
        matches = []
        for entry in entries:
            if Path(entry["file"]).name == "qvcocoafunctions.mm":
                command = shlex.split(entry["command"])
                if f"{target}.dir" in Path(command[command.index("-o") + 1]).parts:
                    matches.append((entry, command))
        if len(matches) != 1:
            raise RuntimeError(f"Expected one Cocoa compile command for {target}, found {len(matches)}")
        entry, command = matches[0]
        yield target, Path(entry["directory"]), command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    env = os.environ.copy()
    for variable in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
                     "OBJCPLUS_INCLUDE_PATH"):
        env.pop(variable, None)

    with tempfile.TemporaryDirectory(prefix="fovelle-thumbnail-compile-") as temp_dir:
        for target, directory, command in cocoa_compile_commands(args.build_dir):
            fallback = ["-idirafter", "/opt/homebrew/include"]
            for index in range(len(command) - 2, 0, -1):
                if command[index:index + 2] == fallback:
                    del command[index:index + 2]
            command[command.index("-o") + 1] = str(Path(temp_dir) / f"{target}.o")
            subprocess.run(command, cwd=directory, env=env, check=True)
            print(f"{target}: compiled qvcocoafunctions.mm", flush=True)


if __name__ == "__main__":
    main()
