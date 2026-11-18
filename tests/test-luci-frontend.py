#!/usr/bin/env python3
"""Fail release builds for incompatible MedveModem LuCI modules.

The r6 blank modem page was caused by a cached shared module missing the
optional setOwnerBadge() API. Check source contracts AND stale-module fallback.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "packages/luci-app-medvemodem/htdocs/luci-static/resources"
COMMON = (BASE / "medvemodem/common.js").read_text(encoding="utf-8")
VIEWS = {
    name: (BASE / f"view/medvemodem/{name}.js").read_text(encoding="utf-8")
    for name in ("dashboard", "modem", "settings")
}
failures = []

shared_version = re.search(r"\bvar VERSION\s*=\s*'([^']+)'", COMMON)
if not shared_version:
    failures.append("common.js has no VERSION")
else:
    for name, source in VIEWS.items():
        match = re.search(r"\bvar VIEW_VERSION\s*=\s*'([^']+)'", source)
        if match and match.group(1) != shared_version.group(1):
            failures.append(f"{name}.js {match.group(1)} != common.js {shared_version.group(1)}")
        elif not match and name in ("dashboard", "modem", "settings"):
            failures.append(f"{name}.js has no VIEW_VERSION")

# A method used by a view must be present in the shared module's public API.
# Search the exported object (not just a private function declaration).
exports = COMMON.split("return baseclass.extend({", 1)
if len(exports) != 2:
    failures.append("common.js: missing baseclass exports")
else:
    public = exports[1]
    # Public method declarations use the form name: implementation.
    api = set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*:", public))
    for name, source in VIEWS.items():
        for method in sorted(set(re.findall(r"\bmm\.([A-Za-z_][A-Za-z0-9_]*)\s*\(", source))):
            if method not in api:
                failures.append(f"{name}.js calls mm.{method}() which common.js does not export")

# A badge is a presentation feature. An already-loaded older common.js must
# never stop the core modem page. This specifically reproduces r6's regression.
modem = VIEWS["modem"]
if "mm.setOwnerBadge(" in modem:
    if not re.search(r"typeof\s+mm\.setOwnerBadge\s*===\s*['\"]function['\"]", modem):
        failures.append("modem.js: optional setOwnerBadge call has no stale-common.js guard")
    if not re.search(r"else\s*\{\s*h\.ownerTag\.hidden\s*=\s*true", modem):
        failures.append("modem.js: missing badge fallback for stale common.js")

if "mm-root mm-modem mm-wait' +" in modem:
    failures.append("modem.js: initial mm-wait hides all modem controls if rendering fails")
if "Unable to display modem data" not in modem:
    failures.append("modem.js: rendering exceptions do not produce a visible error")

if failures:
    print("LuCI frontend contract check FAILED:", file=sys.stderr)
    for message in failures:
        print(" - " + message, file=sys.stderr)
    sys.exit(1)
print("PASS: LuCI module versions, public API and stale-cache fallbacks")
