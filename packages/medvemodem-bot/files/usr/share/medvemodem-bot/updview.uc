'use strict';
// The update notice and the status line of update.uc, drawn from self-update's status alone (no config: testable).
import * as ui from './ui.uc';
import * as lang from './lang.uc';

const T = lang.T;
// OpenWrt package versions use an underscore before alphaN, while GitHub tags use a hyphen.
const VER = /^[0-9][0-9A-Za-z._+~-]{0,40}$/;

function installed(st) { return type(st?.installed?.engine) == 'string' && match(st.installed.engine, VER) ? st.installed.engine : '—'; }

// the notice: the version, what is new (by version, eight lines at most), the button when control is allowed
function view(st, control) {
    let lines = ['🆕 ' + ui.B(T('MedveModem update', 'Обновление MedveModem')),
                 sprintf(T('Version %s is available, %s is installed.', 'Доступна версия %s, установлена %s.'), ui.C(st.latest), ui.C(installed(st)))];
    let news = type(st.highlights) == 'array' ? st.highlights : [], last = null, n = 0;
    if (length(news)) push(lines, '', ui.B(T('What is new', 'Что нового')));
    for (let h in news) {
        if (type(h?.t) != 'string' || type(h?.v) != 'string' || !match(h.v, VER) || n >= 8) continue;
        if (h.v != last) { push(lines, ui.I(h.v)); last = h.v; }
        push(lines, '• ' + ui.esc(substr(h.t, 0, 160)));
        n++;
    }
    if (!control)
        push(lines, '', ui.I(T('Updating from Telegram needs «Control from Telegram»; in LuCI: Settings → Diagnostics and components.',
                               'Обновить из Telegram можно при включённом «Управлении из Telegram»; в LuCI — «Настройки» → «Диагностика и компоненты».')));
    return {text:join('\n', lines),
            markup:control ? {inline_keyboard:[[{text:'⬆️ ' + sprintf(T('Update to %s', 'Обновить до %s'), st.latest), callback_data:'nv'}]]} : null};
}

// a line under «📊 Состояние» while a newer version waits
function statusLine(st) {
    if (!st?.newer || type(st.latest) != 'string' || !match(st.latest, VER)) return '';
    return '\n\n🆕 ' + sprintf(T('MedveModem %s is available (installed %s).', 'Доступна версия MedveModem %s (установлена %s).'),
                               ui.C(st.latest), ui.C(installed(st)));
}

export { VER, installed, view, statusLine };
