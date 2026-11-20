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

    // Verify that saving a new backend is acknowledged in the UI, while the
    // actual owner is checked separately (a started bring-up is not success).
    const switchPage = await browser.newPage();
    const sw = await switchPage.evaluate(async source => {
      String.prototype.format = function (...a) { let i = 0; return this.replace(/%[sd]/g, () => String(a[i++] ?? '')); };
      const _ = text => text;
      const messages = [], toasts = [];
      const config = { device_one: { id: 'ep06', backend: 'modemmanager' } };
      const rpc = { declare: spec => (...args) => {
        if (spec.object === 'uci' && spec.method === 'get')
          return Promise.resolve({ values: JSON.parse(JSON.stringify(config)) });
        if (spec.object === 'uci' && spec.method === 'set') {
          Object.assign(config[args[1]], args[2]);
          return Promise.resolve({});
        }
        if (spec.object === 'uci' && spec.method === 'commit')
          return Promise.resolve({});
        if (spec.object === 'medvemodem' && spec.method === 'bringup')
          return Promise.resolve({result: 'started'});
        return Promise.resolve({});
      }};
      const mm = {
        VERSION: '0.16.0-alpha1-r8', LANG: 'en',
        text: (node, val) => { node.textContent = String(val ?? ''); },
        toast: (key, line, tone) => { toasts.push({line, tone}); },
        failText: error => String(error && error.message || error)
      };
      let view;
      try {
        view = new Function('view', 'rpc', 'poll', 'fs', 'mm', '_', 'E', 'L', source)(
          {extend: value => value}, rpc, {add: () => {}}, {}, mm, _,
          () => document.createElement('div'),
          {env: {},resource: x => x,url: x => x});
      } catch (e) {
        return {error:'compile: ' + e.message};
      }
      view.id = 'ep06';
      view.message = document.createElement('p');
      view.connInfo = {backend: 'modemmanager', interface: 'mm0'};
      const ownerStatus = document.createElement('p');
      view.conn = {
        apn: {value: ''}, auto: {checked: true}, ip: {value: ''},
        backend: {value: 'native'}, ttl: {checked: false},
        autoc: {checked: true}, dns: {value: ''},
        dnsMode: {value: 'add'}, save: {disabled: false},
        ownerStatus
      };
      view.update = () => Promise.resolve();
      window.confirm = () => true;
      try {
        await view.saveConnection();
        const started = !!view.ownerSwitch && view.ownerSwitch.backend === 'native';
        const persisted = config.device_one.backend === 'native';
        const hasBanner = !ownerStatus.hidden && /handover|managing service/i.test(ownerStatus.textContent);
        const hasToast = toasts.length > 0;
        view.followOwnerSwitch({
          device_id: 'ep06', connection: {backend: 'native'},
          backend: 'modemmanager', owner: {control: 'modemmanager'},
          session: {state: 'connected'}
        });
        const notPremature = !!view.ownerSwitch;
        view.followOwnerSwitch({
          device_id: 'ep06', connection: {backend: 'native'},
          backend: 'native', owner: {control: 'netifd-qmi'},
          session: {state: 'connected'}
        });
        const confirmed = !view.ownerSwitch && /modem is connected/i.test(ownerStatus.textContent);
        view.data = {owner: {control: 'netifd-qmi'}, radio_settings: {dialect: 'quectel-ep06'}};
        view.atFresh = () => ({cells: [{earfcn: 1749, pci: 226}]});
        const selected = view.withAtCells({cells: [{earfcn: 800, pci: 1}]}).cells[0].earfcn;
        return {started, persisted, hasBanner, hasToast, notPremature, confirmed,
                nativeCellsVisible: selected === 1749,
                enabled: !view.conn.save.disabled, last: ownerStatus.textContent, toasts};
      } catch (e) {
        return {error:String(e && e.stack || e)};
      }
    }, source);
    assert.equal(sw.error, undefined, JSON.stringify(sw));
    for (const key of ['started','persisted','hasBanner','hasToast','notPremature',
                      'confirmed','nativeCellsVisible','enabled'])
      assert.equal(sw[key], true, key + ': ' + JSON.stringify(sw));
    console.log('PASS chromium EP06 owner selection: save, toast, pending and native QMI readback');
    await switchPage.close();

    // T99W175: its profile deliberately keeps ModemManager first, but a real
    // cdc-wdm/qmi_wwan control endpoint must make the manual native-QMI option available.
    const t99Profile = JSON.parse(fs.readFileSync(path.join(__dirname,
      '../profiles/generic/foxconn-t99w175-qmi.json'), 'utf8'));
    const t99Page = await browser.newPage();
    const t99 = await t99Page.evaluate(({ source, profile }) => {
      String.prototype.format = function (...a) { let i = 0; return this.replace(/%[sd]/g, () => String(a[i++] ?? '')); };
      const _ = value => value;
      const mm = {
        VERSION: '0.16.0-alpha1-r12', LANG: 'en',
        text: (node, value) => { node.textContent = value == null ? '' : String(value); }
      };
      let view;
      try {
        view = new Function('view', 'rpc', 'poll', 'fs', 'mm', '_', 'E', 'L', source)(
          {extend: value => value},
          {declare: spec => () => Promise.resolve({})},
          {add: () => {}}, {}, mm, _, () => document.createElement('div'),
          {env: {}, resource: value => value, url: value => value});
      } catch (e) {
        return {error: 'compile: ' + String(e && e.stack || e)};
      }
      const field = () => ({value: '', checked: false, hidden: false, disabled: false, textContent: '', className: ''});
      const c = view.conn = {
        routerNote: field(), cellOnly: [field()], who: field(),
        apn: field(), ip: field(), nativeOpt: field(), backend: field(), dirty: false,
        ttl: field(), dns: field(), dnsMode: field(), dnsBypassed: field(), dnsField: field(),
        ownNote: field(), qmNote: field(), adoptNote: field(), save: field(), toggle: field(),
        suggest: field(), use: field(), auto: field(), autoc: field()
      };
      c.backend.value = 'modemmanager';
      view.level = 'normal';
      view.alerts = {};
      view.applyUsbConfig = () => {};
      view.applyTtl = () => {};
      view.applyApnAuto = () => {};
      view.applyAlert = () => {};
      view.applyConnection({
        present: true,
        profile: {id: profile.id, status: 'matched'},
        port_roles: {control: {node: '/dev/cdc-wdm0', driver: 'qmi_wwan'}},
        connection: {backend: 'modemmanager', interface: 'mm0', auto_connect: true},
        owner: {control: 'modemmanager', session: 'modemmanager'},
        session: {state: 'connected'}
      });
      return {
        profile: profile.id,
        automaticPriority: profile.policy.owner_priority,
        profileNativeFlags: !!(profile.prefer_owner || profile.fallback_owner || profile.native_qmi),
        nativeVisible: !c.nativeOpt.hidden,
        nativeEnabled: !c.nativeOpt.disabled,
        nativeLabel: c.nativeOpt.textContent,
        selectedBackend: c.backend.value
      };
    }, {source, profile: t99Profile});
    assert.equal(t99.error, undefined, JSON.stringify(t99));
    assert.equal(t99.profile, 'foxconn-t99w175-qmi', JSON.stringify(t99));
    assert.deepEqual(t99.automaticPriority, ['modemmanager', 'netifd-qmi']);
    assert.equal(t99.profileNativeFlags, false);
    assert.equal(t99.nativeVisible, true, JSON.stringify(t99));
    assert.equal(t99.nativeEnabled, true, JSON.stringify(t99));
    assert.match(t99.nativeLabel, /native QMI, without ModemManager/);
    assert.equal(t99.selectedBackend, 'modemmanager');
    console.log('PASS chromium T99W175: native QMI is selectable; automatic ModemManager priority is preserved');
    await t99Page.close();

  } finally {
    await browser.close();
  }
}

run().catch(e => { console.error(e); process.exitCode = 1; });
