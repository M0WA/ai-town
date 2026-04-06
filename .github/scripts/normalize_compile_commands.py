#!/usr/bin/env python3
"""Normalize compile_commands.json paths to be relative to the project root.

Usage: normalize_compile_commands.py <input.json> <output.json>

CMake generates compile_commands.json with absolute paths based on the build
container's filesystem (e.g. /__w/ai-town/ai-town/src/foo.cpp). SonarCloud's
CFamily sensor runs in a separate job where the checkout lives at a different
absolute path (e.g. /home/runner/work/ai-town/ai-town/src/foo.cpp).

This script strips the workspace prefix (GITHUB_WORKSPACE env var, falling back
to CWD) from both "file" and "directory" fields so SonarCloud can match source
files against its indexed tree using relative paths resolved from projectBaseDir.
"""

import json
import os
import sys

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <input.json> <output.json>", file=sys.stderr)
    sys.exit(1)

in_file, out_file = sys.argv[1], sys.argv[2]
ws = (os.environ.get("GITHUB_WORKSPACE") or os.getcwd()).rstrip("/") + "/"

with open(in_file) as f:
    db = json.load(f)

for entry in db:
    for key in ("file", "directory"):
        val = entry.get(key, "")
        # Strip any workspace-rooted absolute prefix so paths become relative.
        # os.path.relpath handles both /__w/... and /home/runner/work/... prefixes.
        if os.path.isabs(val):
            try:
                entry[key] = os.path.relpath(val, ws.rstrip("/"))
            except ValueError:
                pass  # Windows cross-drive edge case — leave unchanged

with open(out_file, "w") as f:
    json.dump(db, f)

print(f"Normalized {len(db)} entries → {out_file}")
