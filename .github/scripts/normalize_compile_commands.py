#!/usr/bin/env python3
"""Normalize compile_commands.json for SonarCloud CFamily analysis.

Usage: normalize_compile_commands.py <input.json> <output.json>

Transformations applied to every entry:

1. Make "file" absolute using the project root.
   CMake/Ninja writes "file" as relative (e.g. "src/foo.cpp").
   Sonar resolves it against "directory" (the build dir) → "build/src/foo.cpp"
   which doesn't exist. Fix: resolve against project root directly.

2. Set "directory" to the project root (absolute).

3. Replace the container workspace prefix in "command" with the project root
   so -I include paths, -D defines, and -c source paths are valid on the
   sonar runner's filesystem.
   The container prefix is derived from the original absolute "file" values
   rather than a regex so spaces in other tokens are not accidentally consumed.

4. Replace /usr/bin/c++ with /usr/bin/g++ in "command".
   SonarCloud CFamily only recognises gcc/g++/clang/clang++ by name.
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

# Derive the container workspace prefix from absolute path fields.
# "directory" in raw CMake output is the build dir: "/__w/ai-town/ai-town/build"
# "file" may also be absolute: "/__w/ai-town/ai-town/src/foo.cpp"
# We strip the known suffix (/build, /src/, /tests/) to get the repo root prefix.
container_prefix = None
for entry in db:
    for key in ("directory", "file"):
        raw = entry.get(key, "")
        if os.path.isabs(raw) and "/__w/" in raw:
            for marker in ("/src/", "/tests/", "/build"):
                idx = raw.find(marker)
                if idx > 0:
                    container_prefix = raw[:idx] + "/"
                    break
        if container_prefix:
            break
    if container_prefix:
        break

if container_prefix:
    print(f"Detected container prefix: {container_prefix}")
else:
    print("No container prefix detected — paths may already be absolute/relative")

for entry in db:
    # 1+2: make "file" absolute under project root; set "directory" to project root
    raw_file = entry.get("file", "")
    if not os.path.isabs(raw_file):
        raw_file = os.path.join(project_root, raw_file)
    elif container_prefix and raw_file.startswith(container_prefix.rstrip("/")):
        raw_file = raw_file.replace(container_prefix.rstrip("/"), project_root, 1)
    entry["file"] = raw_file
    entry["directory"] = project_root

    # 3+4: fix command string
    if "command" in entry:
        cmd = entry["command"]
        if container_prefix:
            cmd = cmd.replace(container_prefix, project_root + "/")
            # Also replace the prefix without trailing slash (e.g. bare -I flags)
            cmd = cmd.replace(container_prefix.rstrip("/"), project_root)
        cmd = cmd.replace("/usr/bin/c++", "/usr/bin/g++")
        entry["command"] = cmd

with open(out_file, "w") as f:
    json.dump(db, f)

print(f"Normalized {len(db)} entries → {out_file}")
