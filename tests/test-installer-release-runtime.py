#!/usr/bin/env python3
"""Execute the installer's actual BusyBox-compatible release selector.

Regression: r7 installer used 'paste', absent on OpenWrt's BusyBox base
image. The old static test copied selection logic and could never catch it.
"""
import pathlib
import re
import shutil
import subprocess
import tempfile

script = pathlib.Path(__file__).resolve().parents[1] / "medvemodem-install.sh"
source = script.read_text()
match = re.search(
    r"TAG=\$\(awk '([\s\S]*?)' \"\$TMP/release-dates\" \"\$TMP/release-tags\" \"\$TMP/release-drafts\"\)",
    source,
)
assert match, "Installer must use self-contained awk selector (no paste)"
awk_program = match.group(1)

cases = [
    (
        "prerelease wins over older stable, qmodem and draft excluded",
        [
            ("2026-11-18T15:00:00Z", "qmodem-4.0-r1", "false"),
            ("2026-11-18T14:00:00Z", "v0.16.0-alpha1-r7", "false"),
            ("2026-11-18T13:00:00Z", "v0.16.0-alpha1-r6", "false"),
            ("2026-11-18T16:00:00Z", "v0.16.0-alpha1-r8", "true"),
        ],
        "v0.16.0-alpha1-r7",
    ),
    (
        "stable wins when newest; no reliance on API listing order",
        [
            ("2026-11-18T11:00:00Z", "v0.16.0-alpha1-r6", "false"),
            ("2026-11-19T11:00:00Z", "v0.16.0", "false"),
            ("2026-11-18T17:00:00Z", "v0.16.0-alpha1-r7", "false"),
        ],
        "v0.16.0",
    ),
    (
        "empty when only other project releases and drafts exist",
        [
            ("2026-11-19T18:00:00Z", "qmodem-5.0", "false"),
            ("2026-11-19T19:00:00Z", "v0.17.0", "true"),
        ],
        "",
    ),
]

with tempfile.TemporaryDirectory() as folder:
    p = pathlib.Path(folder)
    for name, rows, expected in cases:
        files = [p / "dates", p / "tags", p / "drafts"]
        for col, file in enumerate(files):
            file.write_text("".join(row[col] + "\n" for row in rows))
        result = subprocess.run(
            ["awk", awk_program, *(str(f) for f in files)],
            text=True, capture_output=True, check=True, timeout=5,
        )
        actual = result.stdout.strip()
        assert actual == expected, f"{name}: expected {expected!r}, got {actual!r}"
        print("PASS:", name)
assert "paste " not in source.split("resolve_latest_release() {", 1)[1].split("\nprepare_network()", 1)[0].replace("may not have paste (coreutils)", ""), "Forbidden paste in resolver"
print("PASS: no paste dependency")
