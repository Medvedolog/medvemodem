#!/usr/bin/env python3
"""Fail package builds if the whitelist LuCI page has untranslated Russian UI strings.

The page shares settings.js with other tabs. Scope this check to the whitelist
presentation methods; this is intentionally an exact source/PO consistency test,
not a translator for arbitrary JavaScript expressions.
"""
from pathlib import Path
import ast
import re

ROOT = Path(__file__).resolve().parent.parent
JS = ROOT / "packages/luci-app-medvemodem/htdocs/luci-static/resources/view/medvemodem/settings.js"
PO = ROOT / "packages/luci-app-medvemodem/po/ru/medvemodem.po"
POT = ROOT / "packages/luci-app-medvemodem/po/templates/medvemodem.pot"

text = JS.read_text(encoding="utf-8")
# Whitelist-specific methods are grouped together, from its renderer through
# SMS/Telegram alerts and tips. This includes nested labels and table summaries.
start = text.index("\trenderWl: function(")
end = text.index("\t/* the channels table under the dashboard", start)
section = text[start:end]
# The tips card lives right after the channel-table preference, outside the
# grouped methods above; check it separately.
tips_start = text.index("\twlTipsCard: function(")
tips_end = text.index("\t/* the channels table under the dashboard", tips_start)
section += text[tips_start:tips_end]
messages = set()
for match in re.finditer(r"_\('((?:\\.|[^'\\])*)'\)", section):
    # JavaScript string escape handling used in this view (apostrophes, slashes).
    token = match.group(1).replace("\\'", "'").replace("\\\\", "\\")
    messages.add(token)

def catalog(path):
    data = path.read_text(encoding="utf-8")
    result = {}
    # All whitelist msgids here use single-line gettext entries. Block parsing
    # avoids mistaking a subsequent entry's msgstr for an untranslated msgid.
    for block in re.split(r"\n\s*\n", data):
        id_match = re.search(r'^msgid ("(?:\\.|[^"\\])*")$', block, re.M)
        str_match = re.search(r'^msgstr ("(?:\\.|[^"\\])*")$', block, re.M)
        if id_match and str_match:
            key = ast.literal_eval(id_match.group(1))
            val = ast.literal_eval(str_match.group(1))
            result[key] = val
    return result

po = catalog(PO)
pot = catalog(POT)
missing = sorted(s for s in messages if not po.get(s) or s not in pot)
if missing:
    for s in missing:
        print(f"Missing whitelist ru translation / template entry: {s}")
    raise SystemExit(1)
print(f"Whitelist Russian translations complete: {len(messages)} source messages.")
