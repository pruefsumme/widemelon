// Optional real Chromium multi-touch -> production Qt bridge smoke test.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Node.js 22+: node tests/phone_browser_smoke.js build/tests/phone_bridge_test /usr/bin/chromium
'use strict';
const assert = require('node:assert/strict');
const {spawn} = require('node:child_process');
const {mkdtemp, rm} = require('node:fs/promises');
const {tmpdir} = require('node:os');
const path = require('node:path');
const {createInterface} = require('node:readline');
const {setTimeout: delay} = require('node:timers/promises');

async function waitFor(check) {
  for (let i = 0; i < 300; i++) {
    if (await check()) return;
    await delay(10);
  }
  throw Error('Timed out waiting for browser/bridge state: ' + check);
}

(async () => {
  assert(process.argv[2] && process.argv[3] && typeof WebSocket === 'function',
    'Usage (Node.js 22+): node tests/phone_browser_smoke.js build/tests/phone_bridge_test /path/to/chromium');
  const profile = await mkdtemp(path.join(tmpdir(), 'widemelon-browser-smoke-'));
  const children = [];
  let cdp;
  try {
    const bridge = spawn(path.resolve(process.argv[2]), ['--browser-smoke'],
      {env: {...process.env, QT_QPA_PLATFORM: 'offscreen'}, stdio: ['ignore', 'pipe', 'pipe']});
    children.push(bridge);
    let url, state;
    createInterface({input: bridge.stdout}).on('line', line => {
      const message = JSON.parse(line);
      if (message.url) url = message.url;
      else state = message;
    });
    await waitFor(() => url);
    const chrome = spawn(process.argv[3], ['--headless', '--disable-gpu', '--remote-debugging-port=0',
      '--no-first-run', '--no-default-browser-check', `--user-data-dir=${profile}`, 'about:blank'],
      {stdio: ['ignore', 'ignore', 'pipe']});
    children.push(chrome);
    let endpoint;
    createInterface({input: chrome.stderr}).on('line', line => {
      const match = line.match(/DevTools listening on (ws:\/\/\S+)/);
      if (match) endpoint = match[1];
    });
    await waitFor(() => endpoint);
    cdp = new WebSocket(endpoint);
    await new Promise((resolve, reject) => { cdp.onopen = resolve; cdp.onerror = reject; });
    let id = 0;
    const pending = new Map();
    cdp.onmessage = event => {
      const reply = JSON.parse(event.data);
      const callbacks = pending.get(reply.id);
      if (!callbacks) return;
      pending.delete(reply.id);
      if (reply.error) callbacks.reject(Error(JSON.stringify(reply.error)));
      else callbacks.resolve(reply.result);
    };
    function call(method, params = {}, sessionId) {
      return new Promise((resolve, reject) => {
        const requestId = ++id;
        const timeout = setTimeout(() => {
          pending.delete(requestId);
          reject(Error('Chromium command timed out: ' + method));
        }, 3000);
        pending.set(requestId, {
          resolve(value) { clearTimeout(timeout); resolve(value); },
          reject(error) { clearTimeout(timeout); reject(error); }
        });
        cdp.send(JSON.stringify({id: requestId, method, params, sessionId}));
      });
    }
    const {targetId} = await call('Target.createTarget', {url: 'about:blank'});
    const {sessionId} = await call('Target.attachToTarget', {targetId, flatten: true});
    const tab = (method, params) => call(method, params, sessionId);
    await tab('Emulation.setDeviceMetricsOverride', {width: 932, height: 430, deviceScaleFactor: 1, mobile: true});
    await tab('Emulation.setTouchEmulationEnabled', {enabled: true, maxTouchPoints: 5});
    await tab('Page.navigate', {url});
    await waitFor(() => state?.connected);
    const {result} = await tab('Runtime.evaluate', {returnByValue: true, expression: `
      ['.x', '.a', '#screen'].map(selector => {
        const r = document.querySelector(selector).getBoundingClientRect();
        return {x: r.x + r.width / 2, y: r.y + r.height / 2, width: r.width, height: r.height};
      })`});
    const [x, a, screen] = result.value;
    const point = (id, p) => ({id, x: p.x, y: p.y, radiusX: 4, radiusY: 4, force: 1});
    const fingers = [point(1, x), point(2, a)];
    const touch = (type, touchPoints) => tab('Input.dispatchTouchEvent', {type, touchPoints});
    await touch('touchStart', [fingers[0]]);
    await waitFor(() => state.keys === (0xFFF ^ 0x400));
    await touch('touchStart', fingers);
    await waitFor(() => state.keys === (0xFFF ^ 0x401));
    await delay(250);
    assert.equal(state.keys, 0xFFF ^ 0x401, 'both buttons remain held during video streaming');
    await touch('touchEnd', [fingers[1]]);
    await waitFor(() => state.keys === (0xFFF ^ 0x400));
    await touch('touchEnd', []);
    await waitFor(() => state.keys === 0xFFF);
    // Hold A while drawing a continuous stroke with a separate finger.
    await touch('touchStart', [fingers[1]]);
    await waitFor(() => state.keys === 0xFFE);
    const stylus = point(3, {x: screen.x - screen.width / 4, y: screen.y});
    await touch('touchStart', [fingers[1], stylus]);
    await waitFor(() => state.touch !== 0);
    let previousX = state.touch & 255;
    for (let step = 1; step <= 30; step++) {
      stylus.x += screen.width / 100;
      await touch('touchMove', [fingers[1], stylus]);
      await waitFor(() => (state.touch & 255) > previousX);
      assert.equal(state.keys, 0xFFE);
      assert(state.touch >= 0x80000000);
      previousX = state.touch & 255;
    }
    await touch('touchEnd', [stylus]);
    await waitFor(() => state.touch === 0);
    assert.equal(state.keys, 0xFFE, 'stylus release preserves held A');
    await touch('touchEnd', []);
    await waitFor(() => state.keys === 0xFFF);
    assert(state.framesAcked > 2, 'video acknowledgement flow continues during touch input');
    console.log('Real Chromium multi-touch and continuous stylus input passed through the production WebSocket bridge');
  } finally {
    cdp?.close();
    for (const child of children.reverse()) {
      if (child.exitCode !== null || child.signalCode !== null) continue;
      const exited = new Promise(resolve => child.once('exit', resolve));
      child.kill('SIGKILL');
      await exited;
    }
    await rm(profile, {recursive: true, force: true});
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
