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

# R8 field regression: changing EP06 from ModemManager to native QMI did not
# show any action outcome, and native controls compared owner.control to the
# nonexistent literal 'native' rather than netifd-qmi/netifd-mbim. Gate the
# next release against accidentally removing the confirmed handover feedback.
for token in (
    "ownerStage: function(",
    "followOwnerSwitch: function(",
    "this.followOwnerSwitch(d);",
    "mm.toast('backend-' + this.id",
    "c.ownerStatus = E('div'",
    "The managing service was not saved in the modem configuration.",
    "The handover has started. Waiting for the modem to change its managing service",
    "['netifd-qmi', 'netifd-mbim', 'medvemodem-at']",
    "portCtl.node && (/^qmi_wwan",
    "nativeTransport && /^qmi_wwan",
):
    if token not in modem:
        failures.append(f"modem.js: missing EP06 native-owner handover contract: {token}")
if "control !== 'native'" in modem:
    failures.append("modem.js: invalid owner.control === native assumption")

# Settings regressions: the owner badge preference must be part of the common
# Save/Revert form and live at the bottom of the diagnostics tab.
dashboard = (BASE / "view/medvemodem/dashboard.js").read_text(encoding="utf-8")
dashboard_css = (BASE / "medvemodem/dashboard.css").read_text(encoding="utf-8")
settings = VIEWS["settings"]
for token in (
    "this.boxes.owner_badges = box",
    "self.dirty();",
    "localStorage.setItem('mm:owner-badges', f.owner_badges ? '1' : '0')",
    "if (k === 'owner_badges')",
    "this.selects.update_channel = u.channel",
    "update_channel: f.update_channel === 'bench' ? 'bench' : 'release'",
    "row(_('LuCI Telegram bot'), ins.luci_bot)",
    "Save the selected update channel first.",
):
    if token not in settings:
        failures.append(f"settings.js: missing settings/updater contract: {token}")
if "['rec', _('Modems'), [this.ownerBadgePref()" in settings:
    failures.append("settings.js: owner badge preference is not at the bottom of Diagnostics")
if "c.pkgs, c.mmr, this.ownerBadgePref()" not in settings:
    failures.append("settings.js: owner badge preference is not last in Diagnostics")

# Owner badges belong in the lower-right corner of each modem card/page,
# not beside the modem title or in the toolbar/table. HiLink's WEB link
# already identifies that interface and must not be duplicated as an owner.
if "refs.ownerTag\n" not in dashboard:
    failures.append("dashboard.js: owner badge is not appended after card actions")
if "mm-mcard > .mm-owner-tag" not in dashboard_css:
    failures.append("dashboard.css: dashboard owner badge is not bottom-right aligned")
if "h.ownerTag]);" not in modem:
    failures.append("modem.js: owner badge is not appended after modem hero actions")
if "mm-hero > .mm-owner-tag" not in dashboard_css:
    failures.append("dashboard.css: modem-page owner badge is not bottom-right aligned")
if "refs.ownerTag, refs.autofixBadge" in dashboard:
    failures.append("dashboard.js: owner badge remains in the top icon row")
if "card.ownerTag.cloneNode(true)" in dashboard:
    failures.append("dashboard.js: owner badge remains in the modem table")
if "[h.title, h.ownerTag, nm.open" in modem:
    failures.append("modem.js: owner badge remains beside the modem title")
if "hilink:" in COMMON or "['WEB'" in COMMON:
    failures.append("common.js: redundant WEB owner badge is still defined")
if "var VIEW_VERSION = '0.16.0-alpha1-r10';" not in settings:
    failures.append("settings.js: expected r9 source version")

if failures:
    print("LuCI frontend contract check FAILED:", file=sys.stderr)
    for message in failures:
        print(" - " + message, file=sys.stderr)
    sys.exit(1)
print("PASS: LuCI module versions, public API and stale-cache fallbacks")
