#!/usr/bin/env python3
"""Regression contracts for QModem handover and failed adoption ownership."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
ENGINE = (ROOT / "src/medvemodemd.c").read_text(encoding="utf-8")
DASH = (ROOT / "packages/luci-app-medvemodem/htdocs/luci-static/resources/view/medvemodem/dashboard.js").read_text(encoding="utf-8")
failures = []

def require(source, token, label):
    if token not in source:
        failures.append(f"{label}: missing {token}")

# A previous QModem lease must be releasable when the operator switches to
# auto, except when auto would still deliberately select QModem for this profile.
leave = ENGINE.split("static bool qmodem_leaving(", 1)[1].split("static bool bringup_txn(", 1)[0]
require(leave, 'strcmp(choice, "auto")', "qmodem_leaving")
require(leave, 'strcmp(preferred, "qmodem")', "qmodem_leaving")
require(leave, 'qmodem_installed()', "qmodem_leaving")
require(leave, "sec[0] = '\\0'", "qmodem_leaving")

# If QModem never made a section, the engine must clear its failed lease
# without invoking modem_scan.sh remove on a fabricated section name.
go = ENGINE.split("static bool bringup_go(", 1)[1].split("static bool bringup_txn(", 1)[0]
require(go, "bool have_section = sec[0] != '\\0'", "bringup_go")
require(go, 'job_log(have_section ? "qmodem-remove" : "qmodem-remove-skipped"', "bringup_go")
require(go, "if (!have_section)\n\t\t\tbringup_after_qmodem(0);", "bringup_go")

# MM-first auto and explicit ModemManager both drop the engine's QModem lease,
# release inhibition, and wait for ModemManager to rediscover the device.
after = ENGINE.split("static void bringup_after_qmodem(", 1)[1].split("static bool bringup_go(", 1)[0]
require(after, 'strcmp(choice, "auto")', "bringup_after_qmodem")
require(after, 'strcmp(preferred, "modemmanager")', "bringup_after_qmodem")
require(after, "mm_return_wait(job.device_id, job.generation)", "bringup_after_qmodem")
require(after, "inhibits_sync()", "bringup_after_qmodem")

# A configured backend is not an owner until the QModem scanner exposes the
# matching modem-device section. A missing section is surfaced as failed adoption.
served = ENGINE.split("bool ours = native && backend", 1)[1].split("if (ours && qsec)", 1)[0]
require(served, "if (qsec && !ours)", "served owner")
require(served, 'json_object_new_string("unresolved")', "served owner")
require(served, '"adoption_failed"', "served owner")
require(served, '"adoption_pending"', "served owner")

# The dashboard explains failed adoption and does not present QModem telemetry
# as stale data when QModem never took the modem.
require(DASH, "var VIEW_VERSION = '0.16.0-alpha1-r14';", "dashboard version")
require(DASH, "qmodem.adoption_failed", "dashboard")
require(DASH, "QModem is selected, but no modem-device section was created", "dashboard")
require(DASH, "qmodem.adoption_pending", "dashboard")
require(DASH, "!qmodem.adoption_failed", "dashboard")

if failures:
    print("QModem handover regression check FAILED:", file=sys.stderr)
    for failure in failures:
        print(" - " + failure, file=sys.stderr)
    sys.exit(1)
print("PASS: QModem auto hand-back, failed adoption lease cleanup and owner telemetry")
