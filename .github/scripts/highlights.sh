#!/bin/sh
# The short highlights of the last versions, for the router's update card and the bot (the owner, 2026-11-14: «с
# демонстрацией хайлайтов обновления кратких с гитхаба»): from docs/CHANGELOG.md, the newest 12 sections, each as
#   ## <version>
#   - <the bold lead of a bullet, or its first words>
# Published as the release's HIGHLIGHTS.txt beside the archives (and in its SHA256SUMS).
#   highlights.sh [CHANGELOG]
set -eu
awk '
	/^## [0-9]/ { if (++n > 12) exit; split($0, w, " "); print "## " w[2]; next }
	n && /^- / {
		t = substr($0, 3)
		if (match(t, /^\*\*[^*]+\*\*/)) t = substr(t, 3, RLENGTH - 4)
		else { sub(/[:.].*$/, "", t); if (length(t) > 110) t = substr(t, 1, 107) "…" }
		gsub(/`|\*\*/, "", t)
		print "- " t
	}
' "${1:-docs/CHANGELOG.md}"
