'use strict';
// The bot's language (the owner, 2026-10-23: the LuCI pages and the bot in English and Russian). Every text is
// written in both, side by side, so one cannot lose its pair; T() picks one as the text is made. main.lang is
// 'ru', 'en' or 'auto' (the default): auto follows the admin's Telegram app, as their latest message or button
// press says (language_code), and is Russian until one has come.
let current = 'ru';

function set(lang) { current = lang == 'en' ? 'en' : 'ru'; }
function get() { return current; }
// Telegram's language_code: «ru», «ru-RU», «en», «de»…; null when the update has none
function fromTelegram(code) {
    if (type(code) != 'string' || !length(code)) return null;
    return substr(code, 0, 2) == 'ru' ? 'ru' : 'en';
}
function T(en, ru) { return current == 'en' ? en : ru; }
// a pair from a table, [en, ru]; null stays null
function W(pair) { return pair == null ? null : current == 'en' ? pair[0] : pair[1]; }

export { set, get, fromTelegram, T, W };
