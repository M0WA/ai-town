#!/usr/bin/env python3
"""Normalize compile_commands.json for SonarCloud CFamily analysis.

Usage: normalize_compile_commands.py <input.json> <output.json>

Three transformations applied to every entry:

1. Make "file" absolute using the project root.
   CMake writes "file" as relative (e.g. "src/foo.cpp") when using Ninja.
   Sonar resolves it against "directory", but "directory" is the build dir —
   so "build/src/foo.cpp" is produced and no source file matches.
   Fix: resolve "file" against the project root directly.

2. Make "directory" the project root (absolute).
   After fix 1, "directory" is only used as the working dir for the compiler
   command, which still works with the project root as CWD.

3. Replace /usr/bin/c++ with /usr/bin/g++ in "command".
   SonarCloud CFamily only recognises gcc/g++/clang/clang++ by name.
   The c++ alias is not in its supported-compiler list → all 138 units are
   reported as "unsupported" and skipped.
"""

import json
import os
import sys

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <input.json> <output.json>", file=sys.stderr)
    sys.exit(1)

in_file, out_file = sys.argv[1], sys.argv[2]
project_root = os.path.abspath(
    os.environ.get("GITHUB_WORKSPACE") or os.getcwd()
)

with open(in_file) as f:
    db = json.load(f)

for entry in db:
    # 1+2: resolve "file" to absolute against project root; set "directory" to root
    raw_file = entry.get("file", "")
    if not os.path.isabs(raw_file):
        raw_file = os.path.join(project_root, raw_file)
    entry["file"] = raw_file
    entry["directory"] = project_root

    # 3: replace /usr/bin/c++ with /usr/bin/g++ so CFamily recognises the compiler
    if "command" in entry:
        entry["command"] = entry["command"].replace("/usr/bin/c++", "/usr/bin/g++")

with open(out_file, "w") as f:
    json.dump(db, f)

print(f"Normalized {len(db)} entries → {out_file}")
