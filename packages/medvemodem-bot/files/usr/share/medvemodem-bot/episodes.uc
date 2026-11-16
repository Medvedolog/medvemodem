'use strict';

// USB and network incidents are correlation windows, not isolated log lines.
// Records hold only opaque internal device keys and enums, never raw events.
function isBad(kind, current) {
    if (kind == 'present') return current === false;
    if (kind == 'session') return current == 'disconnected' || current == 'failed' || current == 'down';
    if (kind == 'reach') return current == 'limited' || current == 'unavailable' || current == 'offline';
    if (kind == 'engine') return current == 'offline';
    return false;
}

function update(map, id, kind, oldValue, newValue, now, mode, emit) {
    let ident = id + '/' + kind;
    let entry = map[ident];
    let bad = isBad(kind, newValue);
    // a session that is connecting or an internet that is being checked has not come back yet: neither state
    // closes an outage, or «down → checking → down» reads as a recovery and a blip
    let good = !bad && newValue != 'unknown' && newValue != '' && newValue != null &&
               newValue != 'connecting' && newValue != 'checking';
    if (bad) {
        // the 5 min window counts short blips; an outage still open keeps its start (audit r101 R03: limited → unavailable
        // → available over 10 min read as 200 s)
        if (!entry || (!entry.open && now - entry.window >= 300))
            entry = map[ident] = {id,kind,window:now,since:now,open:false,notice:false,blips:0,flapNotified:false,last:now};
        if (!entry.open) {
            entry.open = true;
            entry.since = now;
            entry.last = now;
            entry.level = newValue;
            if (mode == 'all') { emit('outage',kind,id,0,newValue); entry.notice = true; }
        } else if (entry.level != newValue && !entry.notice)
            entry.level = newValue;	/* not told yet: the notice names the state it is in now */
        else if (entry.level == 'limited' && newValue != 'limited' && entry.notice) {
            // whitelists told, now nothing at all: that is news of its own
            entry.level = newValue;
            emit('outage', kind, id, now - entry.since, newValue);
        }
        return;
    }
    if (!entry || !entry.open || !good) return;
    entry.open = false;
    entry.last = now;
    if (entry.notice) {
        if (mode != 'off') emit('recovery', kind, id, now - entry.since, entry.level);
        entry.notice = false;
    } else {
        entry.blips++;
        if (entry.blips >= 3 && !entry.flapNotified && mode == 'smart') {
            emit('flapping', kind, id, entry.blips);
            entry.flapNotified = true;
        }
    }
}

function flush(map, now, mode, threshold, emit) {
    for (let ident, entry in map) {
        if (!entry.open) {
            if (now - entry.last > 300) delete map[ident];
            continue;
        }
        // critical: the router's internet as a whole only, not a modem's own check (docs/BOT-NOTIFICATIONS-PLAN.md
        // step 7; audit r101 R02: a backup's reach was told in critical)
        if (entry.notice || mode == 'off' || (mode == 'critical' && !(entry.kind == 'reach' && entry.id == '-'))) continue;
        // a threshold per episode: a backup modem waits longer while the internet works through another channel
        if (now - entry.since < (type(threshold) == 'function' ? threshold(entry) : threshold)) continue;
        emit('outage', entry.kind, entry.id, now - entry.since, entry.level);
        entry.notice = true;
    }
}

// the restart's quiet is over (audit r101 R01): its ups and downs are forgotten, an outage still open stays and is told
// by flush() as any other
function settle(map) {
    for (let ident, entry in map)
        if (!entry.open) delete map[ident];
        else { entry.blips = 0; entry.flapNotified = false; }
}

// a state already bad when nothing was compared yet (the bot started, or the restart's quiet ended, with no internet):
// an episode from now, told after the usual wait
function seed(map, id, kind, current, now) {
    let entry = map[id + '/' + kind];
    if (isBad(kind, current) && !(entry && entry.open))
        update(map, id, kind, null, current, now, 'smart', () => null);
}

// ucode as pinned by OpenWrt 24.10 and 25.12 does not parse `export function` declarations
export { isBad, update, flush, settle, seed };
