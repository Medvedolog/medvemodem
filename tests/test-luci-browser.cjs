#!/usr/bin/env node
'use strict';
// Chromium smoke test of the REAL LuCI modem view, with mocked router RPC.
// Catch blank-page regressions even if API contract/static checks pass.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');

const source = fs.readFileSync(path.join(__dirname,
  '../packages/luci-app-medvemodem/htdocs/luci-static/resources/view/medvemodem/modem.js'), 'utf8');

async function run() {
  const version = source.match(/var VIEW_VERSION = '([^']+)'/);
  assert.ok(version, 'LuCI view VERSION missing');
  const browser = await chromium.launch({ headless: true });
  try {
    for (const failureMode of ['rpc-error', 'render-error']) {
      const page = await browser.newPage();
      await page.route('**/modem*', route => route.fulfill({
        status: 200, contentType: 'text/html',
        body: '<!doctype html><html><body></body></html>'
      }));
      await page.goto('http://router.test/modem#test-modem');
      const result = await page.evaluate(async ({ source, failureMode, viewVersion }) => {
        const failures = [];
        window.addEventListener('error', e => failures.push(e.message));
        // LuCI extends String.prototype with format(); the real view uses it in error messages.
        String.prototype.format = function (...args) { let i = 0; return this.replace(/%[sd]/g, () => String(args[i++] ?? '')); };
        const _ = str => str;
        const E = (tag, attrs, children) => {
          if (attrs == null || typeof attrs !== 'object' || Array.isArray(attrs) || attrs instanceof Node)
            { children = attrs; attrs = {}; }
          const node = document.createElement(tag);
          for (const [k, v] of Object.entries(attrs || {})) {
            if (k === 'hidden') node.hidden = true;
            else if (k === 'class') node.className = v;
            else if (k === 'click') node.addEventListener('click', v);
            else if (k !== 'children') node.setAttribute(k, String(v));
          }
          const add = x => {
            if (x == null || x === false) return;
            if (Array.isArray(x)) x.forEach(add);
            else node.appendChild(x instanceof Node ? x : document.createTextNode(String(x)));
          };
          add(children);
          return node;
        };
        const mm = {
          VERSION: viewVersion, LANG: 'en',
          level: () => 'normal',
          text: (node, str) => { node.textContent = str == null ? '' : String(str); },
          failText: error => String(error && error.message || error),
          pageHead: () => E('header', {}, 'MedveModem'),
          dismissible: () => {}, tabsHint: () => () => {}, busy: fn => fn,
          cacheGet: () => null, cacheSet: () => {},
          staleWarn: () => {}
        };
        const rpc = { declare: spec => () => failureMode === 'rpc-error'
          ? Promise.reject(new Error('RPC unavailable'))
          : Promise.resolve({ device_id: 'test-modem', model: 'Test modem', health: {state: 'ok'} }) };
        let view;
        try {
          view = new Function('view', 'rpc', 'poll', 'fs', 'mm', '_', 'E', 'L', source)(
            {extend: value => value}, rpc, {add: () => {}}, {},
            mm, _, E, {env: {}, resource: s => '/static/' + s, url: s => '/' + s});
        } catch (err) { return {mode: failureMode, compileError: String(err), failures}; }
        const fakeNode = () => E('section', {}, 'panel');
        view.makeHero = function () { return {node: E('section', {class: 'mm-hero'}, 'hero'), title: E('h2')}; };
        for (const name of ['overview', 'radio', 'smsTab', 'esimCard', 'settingsTab', 'atTab', 'service']) {
          view[name] = () => fakeNode();
        }
        view.esimShow = () => {};
        view.applyAll = function () { throw new Error('injected applyAll failure'); };
        let root;
        try { root = view.render(); document.body.appendChild(root); }
        catch (err) { return {mode: failureMode, renderError: String(err), failures}; }
        await new Promise(resolve => setTimeout(resolve, 150));
        const notice = root.querySelector('.mm-notice');
        const tabs = root.querySelector('.mm-tabs-wrap');
        return {mode: failureMode, errors: failures, notice: notice && notice.textContent,
          noticeVisible: Boolean(notice && !notice.hidden),
          tabsVisible: Boolean(tabs && !tabs.hidden && getComputedStyle(tabs).display !== 'none'),
          blankWait: root.classList.contains('mm-wait')};
      }, {source, failureMode, viewVersion: version[1]});
      assert.equal(result.compileError, undefined, JSON.stringify(result));
      assert.equal(result.renderError, undefined, JSON.stringify(result));
      assert.equal(result.noticeVisible, true, JSON.stringify(result));
      assert.equal(result.tabsVisible, true, JSON.stringify(result));
      assert.equal(result.blankWait, false, JSON.stringify(result));
      assert.match(result.notice, failureMode === 'render-error' ? /injected applyAll failure/ : /Loading|reply|answer|unavailable/i);
      console.log('PASS chromium LuCI modem page:', failureMode);
      await page.close();
    }
    // Execute the exact badge fallback statement from the actual view, inside
    // Chromium, with a previously cached common.js that lacks the new API.
    const page = await browser.newPage();
    const badge = await page.evaluate(source => {
      const match = source.match(/if \(typeof mm\.setOwnerBadge === 'function'\)[\s\S]*?else \{ h\.ownerTag\.hidden = true; h\.ownerTag\.textContent = ''; \}/);
      if (!match) return {ok: false, reason: 'badge fallback not present'};
      const node = document.createElement('span');
      const fn = new Function('mm', 'h', 'd', match[0]);
      try { fn({}, {ownerTag: node}, {owner: {control: 'modemmanager'}}); }
      catch (err) { return {ok: false, reason: String(err)}; }
      return {ok: node.hidden, text: node.textContent};
    }, source);
    assert.equal(badge.ok, true, JSON.stringify(badge));
    console.log('PASS chromium LuCI stale common.js owner badge fallback');
    await page.close();
  } finally {
    await browser.close();
  }
}

run().catch(e => { console.error(e); process.exitCode = 1; });
