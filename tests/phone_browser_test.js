// Browser event/state regressions against the actual bundled controller script.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

function page(storageBlocked = false) {
  const elements = new Map();
  function element() {
    const handlers = new Map();
    return {
      handlers, dataset: {}, style: {setProperty() {}},
      classList: {add() {}, remove() {}, toggle() {}, contains() { return false; }},
      addEventListener(name, fn) { handlers.set(name, fn); },
      fire(name, extra = {}) { handlers.get(name)?.({preventDefault() {}, ...extra}); },
      setPointerCapture() {}, setAttribute() {}, focus() {},
      getBoundingClientRect() { return {left: 0, top: 0, width: 256, height: 192}; }
    };
  }
  const document = element();
  document.getElementById = id => {
    if (!elements.has(id)) elements.set(id, element());
    return elements.get(id);
  };
  document.querySelectorAll = () => [];
  const draws = [];
  document.getElementById('screen').getContext = () => ({drawImage(bitmap) { draws.push(bitmap.id); }});
  const window = element();
  const sockets = [];
  class WebSocket {
    static OPEN = 1;
    static CLOSED = 3;
    constructor() { this.readyState = 0; this.sent = []; sockets.push(this); }
    send(text) { this.sent.push(JSON.parse(text)); }
    open() { this.readyState = 1; this.onopen(); }
    message(data) { this.onmessage({data: typeof data === 'object' && !(data instanceof ArrayBuffer)
      ? JSON.stringify(data) : data}); }
    close(reason = '') { this.readyState = 3; this.onclose({reason}); }
  }
  let clock = 0, timerId = 0;
  const timers = new Map();
  const timer = (fn, delay, repeat = false) => {
    const id = ++timerId;
    timers.set(id, {fn, delay, repeat, at: clock + delay});
    return id;
  };
  function tick(ms) {
    const end = clock + ms;
    for (;;) {
      const entry = [...timers].filter(([, t]) => t.at <= end).sort((a, b) => a[1].at - b[1].at)[0];
      if (!entry) break;
      const [id, t] = entry;
      clock = t.at;
      timers.delete(id);
      if (t.repeat) timers.set(id, {...t, at: clock + t.delay});
      t.fn();
    }
    clock = end;
  }
  const decodes = [];
  const replacedUrls = [];
  const storage = new Map();
  const sandbox = {document, window, WebSocket, ArrayBuffer, DataView, Uint8Array, Blob,
    URLSearchParams, performance: {now: () => clock},
    location: {host: '127.0.0.1:24800', hash: '#pair=' + 'A'.repeat(43), pathname: '/', search: ''},
    history: {replaceState(a, b, url) { replacedUrls.push(url); }},
    sessionStorage: {
      getItem(key) { if (storageBlocked) throw Error('storage disabled'); return storage.get(key); },
      setItem(key, value) { if (storageBlocked) throw Error('storage disabled'); storage.set(key, value); },
      removeItem(key) { if (storageBlocked) throw Error('storage disabled'); storage.delete(key); }
    },
    setTimeout: (fn, delay) => timer(fn, delay), clearTimeout: id => timers.delete(id),
    setInterval: (fn, delay) => timer(fn, delay, true),
    createImageBitmap: () => new Promise(resolve => decodes.push(resolve))};
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../src/frontend/qt_sdl/phone/app.js'), 'utf8'), sandbox);
  return {document, window, sockets, tick, decodes, draws, storage, replacedUrls};
}

function frame(sequence) {
  const buffer = new ArrayBuffer(28);
  const view = new DataView(buffer);
  view.setUint32(0, 0x574d4632);
  view.setUint32(4, sequence, true);
  view.setUint8(20, 1);
  return buffer;
}
async function decode(p, id) {
  let closed = false;
  p.decodes.shift()({id, close() { closed = true; }});
  for (let i = 0; i < 5; i++) await Promise.resolve();
  assert(closed, 'decoded bitmap must be released');
}

(async () => {
  const p = page(true);
  assert.deepEqual(p.replacedUrls, ['/'], 'QR fragment erased even if storage is disabled');
  let socket = p.sockets[0];
  socket.open();
  p.tick(200);
  assert.equal(socket.sent.length, 1, 'only auth is sent before hello');
  socket.message({v: 2, type: 'hello'});
  p.tick(16);
  const screen = p.document.getElementById('screen');
  screen.fire('pointerdown', {pointerId: 1, clientX: 10, clientY: 20});
  for (let i = 0; i < 1000; i++) screen.fire('pointermove', {pointerId: 1, clientX: 20, clientY: 30});
  const before = socket.sent.length;
  p.tick(16);
  assert.equal(socket.sent.length, before + 1, 'pointer bursts produce one snapshot');
  assert.equal(socket.sent.at(-1).touch.active, true);
  p.window.fire('pagehide');
  assert.equal(socket.sent.at(-2).touch.active, false, 'page exit releases touch immediately');

  socket.message(frame(1));
  socket.message(frame(2));
  socket.message(frame(3));
  assert.equal(p.decodes.length, 1, 'only one decode at a time');
  await decode(p, 1);
  assert.equal(p.decodes.length, 1);
  await decode(p, 3);
  assert.deepEqual(p.draws, [1, 3], 'newest pending frame replaces older pending frame');
  assert.deepEqual(socket.sent.filter(m => m.type === 'frameAck').map(m => m.seq), [1, 3]);

  socket.message(frame(4));
  const stale = socket;
  socket.close();
  p.tick(250);
  socket = p.sockets.at(-1);
  socket.open();
  socket.message({v: 2, type: 'hello'});
  await decode(p, 4);
  assert.deepEqual(p.draws, [1, 3], 'old connection decode must not draw after reconnect');
  assert.equal(socket.sent.filter(m => m.type === 'frameAck').length, 0);
  stale.onclose({reason: 'Authentication failed'});
  p.tick(16);
  assert(socket.sent.some(m => m.type === 'input'), 'stale callbacks cannot deauthorize new connection');
  socket.close('Authentication failed');
  assert.equal(p.document.getElementById('pairing').hidden, false);

  const busy = page();
  for (const delay of [250, 500, 1000, 2000]) {
    const count = busy.sockets.length;
    const candidate = busy.sockets.at(-1);
    candidate.open();
    candidate.close('Pairing unavailable');
    busy.tick(delay - 1);
    assert.equal(busy.sockets.length, count, 'handshake alone must not reset reconnect backoff');
    busy.tick(1);
    assert.equal(busy.sockets.length, count + 1);
  }
  console.log('Phone browser input, decoding, credential storage and reconnect regressions passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
