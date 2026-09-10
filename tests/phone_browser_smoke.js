// Optional real Chromium multi-touch -> production Qt bridge smoke test.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Node.js 22+: node tests/phone_browser_smoke.js build/tests/phone_bridge_test /usr/bin/chromium
'use strict';
const assert = require('node:assert/strict');
const {spawn, execFileSync} = require('node:child_process');
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
  const benchmark = process.argv.includes('--benchmark');
  try {
    const bridgeArgs = ['--browser-smoke'];
    if (process.argv.includes('--dialog')) bridgeArgs.push('--benchmark-dialog');
    const bridge = spawn(path.resolve(process.argv[2]), bridgeArgs,
      {env: {...process.env, QT_QPA_PLATFORM: 'offscreen'}, stdio: ['ignore', 'pipe', 'pipe']});
    children.push(bridge);
    let url, state;
    let guiMaxMs = 0;
    createInterface({input: bridge.stdout}).on('line', line => {
      const message = JSON.parse(line);
      if (message.url) url = message.url;
      else { state = message; guiMaxMs = Math.max(guiMaxMs, message.guiTickMs || 0); }
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
      if (reply.method === 'Fetch.requestPaused') {
        call('Fetch.fulfillRequest', {requestId: reply.params.requestId, responseCode: 200,
          responseHeaders: [{name: 'Content-Type', value: 'text/javascript'}],
          body: benchmarkScript.toString('base64')}, reply.sessionId).catch(error => { throw error; });
        return;
      }
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
    await tab('Page.enable');
    let benchmarkScript;
    if (benchmark && process.env.WIDEMELON_BENCH_REVISION) {
      assert(/^[a-fA-F0-9]{7,40}$/.test(process.env.WIDEMELON_BENCH_REVISION), 'Benchmark revision must be a commit hash');
      benchmarkScript = execFileSync('git', ['show',
        `${process.env.WIDEMELON_BENCH_REVISION}:src/frontend/qt_sdl/phone/app.js`]);
      await tab('Fetch.enable', {patterns: [{urlPattern: '*/app.js', requestStage: 'Request'}]});
    }
    if (benchmark) {
      await tab('Emulation.setCPUThrottlingRate', {rate: Number(process.env.WIDEMELON_BENCH_CPU || 1)});
      await tab('Page.addScriptToEvaluateOnNewDocument', {source: `
        window.bench = {arrivals: [], decodes: [], draws: [], inputs: 0, ack: 0};
        const NativeSocket = WebSocket;
        window.WebSocket = class extends NativeSocket {
          constructor(...args) {
            super(...args);
            this.addEventListener('message', e => {
              if (typeof e.data !== 'string') bench.arrivals.push(performance.now());
            });
          }
          send(data) {
            const message = JSON.parse(data);
            if (message.type === 'input') bench.inputs++;
            if (message.type === 'frameAck') bench.ack++;
            return super.send(data);
          }
        };
        const decode = createImageBitmap;
        window.createImageBitmap = async (...args) => {
          const started = performance.now();
          const bitmap = await decode(...args);
          bench.decodes.push(performance.now() - started);
          return bitmap;
        };
        const draw = CanvasRenderingContext2D.prototype.drawImage;
        CanvasRenderingContext2D.prototype.drawImage = function(...args) {
          bench.draws.push(performance.now());
          return draw.apply(this, args);
        };
        document.addEventListener('pointerdown', e => { window.benchPointer = e.pointerId; }, true);
      `});
    }
    await tab('Emulation.setDeviceMetricsOverride', {width: 932, height: 430, deviceScaleFactor: 1, mobile: true});
    await tab('Emulation.setTouchEmulationEnabled', {enabled: true, maxTouchPoints: 5});
    await tab('Page.navigate', {url});
    await waitFor(() => state?.connected);
    await waitFor(async () => (await tab('Runtime.evaluate', {returnByValue: true,
      expression: "document.getElementById('pairing')?.hidden === true"})).result.value);
    const {result} = await tab('Runtime.evaluate', {returnByValue: true, expression: `
      ['.x', '.a', '#screen'].map(selector => {
        const r = document.querySelector(selector).getBoundingClientRect();
        return {x: r.x + r.width / 2, y: r.y + r.height / 2, width: r.width, height: r.height};
      })`});
    const [x, a, screen] = result.value;
    const point = (id, p) => ({id, x: p.x, y: p.y, radiusX: 4, radiusY: 4, force: 1});
    const fingers = [point(1, x), point(2, a)];
    const touch = (type, touchPoints) => tab('Input.dispatchTouchEvent', {type, touchPoints});
    if (benchmark) {
      const evaluate = async expression => (await tab('Runtime.evaluate', {expression, returnByValue: true})).result.value;
      for (const phase of ['idle', 'motion', 'buttons']) {
        if (phase === 'motion') {
          await touch('touchStart', [point(3, screen)]);
          await waitFor(() => state.touch >= 0x80000000);
          await evaluate(`window.benchMove = setInterval(() => {
            const r = document.getElementById('screen').getBoundingClientRect();
            document.getElementById('screen').dispatchEvent(new PointerEvent('pointermove', {
              pointerId: benchPointer, clientX: r.x + r.width * (0.5 + 0.2 * Math.sin(performance.now() / 200)),
              clientY: r.y + r.height / 2
            }));
          }, 1000 / 120)`);
        }
        if (phase === 'buttons') await touch('touchStart', fingers);
        await delay(500);
        const before = {...state};
        guiMaxMs = 0;
        await evaluate('window.bench = {arrivals: [], decodes: [], draws: [], inputs: 0, ack: 0}');
        const start = performance.now();
        await delay(6000);
        const seconds = (performance.now() - start) / 1000;
        const browser = await evaluate('bench');
        const gaps = browser.draws.slice(1).map((value, index) => value - browser.draws[index]).sort((a, b) => a - b);
        const result = {revision: process.env.WIDEMELON_BENCH_REVISION || 'working', phase,
          cpu: process.env.WIDEMELON_BENCH_CPU || '1', dialog: process.argv.includes('--dialog'),
          offeredFps: (state.framesOffered - before.framesOffered) / seconds,
          encodedFps: (state.framesEncoded - before.framesEncoded) / seconds,
          sentFps: (state.framesSent - before.framesSent) / seconds,
          ackedFps: (state.framesAcked - before.framesAcked) / seconds,
          receivedFps: browser.arrivals.length / seconds, displayedFps: browser.draws.length / seconds,
          inputsPerSecond: browser.inputs / seconds, encodeMs: state.encodeMs,
          decodeMs: browser.decodes.reduce((a, b) => a + b, 0) / browser.decodes.length,
          drawGap95Ms: gaps[Math.floor(gaps.length * 0.95)], guiMaxMs};
        console.log(JSON.stringify(result));
        assert(result.offeredFps >= 28.5 && result.offeredFps <= 31, 'test frame source must stay near 30 FPS');
        assert(result.displayedFps >= 28.5, 'stream must stay near 30 FPS during each six-second phase');
        if (phase === 'motion') assert(result.inputsPerSecond >= 20, 'benchmark must actually exercise input');
        await evaluate('clearInterval(window.benchMove)');
        if (phase !== 'idle') await touch('touchEnd', []);
      }
      return;
    }
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
    await rm(profile, {recursive: true, force: true, maxRetries: 5, retryDelay: 100});
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
