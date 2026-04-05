#!/usr/bin/env python3
"""Convert lcov .info file to SonarQube Generic Coverage XML.

Usage: lcov_to_sonar.py <input.info> <output.xml>

sonar.coverageReportPaths requires Generic Coverage XML format:
  https://docs.sonarsource.com/sonarqube/latest/analyzing-source-code/test-coverage/generic-test-data/

lcov_cobertura generates Cobertura XML which SonarScanner rejects with exit code 3.
"""

import sys

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <input.info> <output.xml>", file=sys.stderr)
    sys.exit(1)

lcov_file, out_file = sys.argv[1], sys.argv[2]

# Parse lcov DA records: DA:<lineno>,<hits>[,<checksum>]
# A line is "covered" if hits > 0. If the same line appears in multiple DA
# records (e.g. inlined functions), mark it covered if ANY record has hits > 0.
files: dict[str, dict[str, bool]] = {}
current: str | None = None

with open(lcov_file) as f:
    for raw in f:
        line = raw.rstrip()
        if line.startswith("SF:"):
            current = line[3:]
            files.setdefault(current, {})
        elif line.startswith("DA:") and current is not None:
            parts = line[3:].split(",", 2)
            lineno, hits = parts[0], int(parts[1])
            # Sticky: once covered, stay covered
            if lineno not in files[current] or hits > 0:
                files[current][lineno] = hits > 0
        elif line == "end_of_record":
            current = None

with open(out_file, "w") as f:
    f.write('<coverage version="1">\n')
    for path in sorted(files):
        f.write(f'  <file path="{path}">\n')
        for lineno in sorted(files[path], key=int):
            covered = "true" if files[path][lineno] else "false"
            f.write(f'    <lineToCover lineNumber="{lineno}" covered="{covered}"/>\n')
        f.write("  </file>\n")
    f.write("</coverage>\n")

print(f"Generated {out_file} with {len(files)} files")
