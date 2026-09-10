// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later

(() => {
  'use strict';
  const status = document.getElementById('status');
  const metrics = document.getElementById('metrics');
  const hud = document.getElementById('hud');
  const canvas = document.getElementById('screen');
  const context = canvas.getContext('2d', {alpha: false});
  const dpad = document.getElementById('dpad');
  const pairing = document.getElementById('pairing');
  const pairingForm = document.getElementById('pairing-form');
  const pairingCode = document.getElementById('pairing-code');
  const pointers = new Map();
  let socket = null;
  let buttons = 0;
  let hotkeys = 0;
  let touch = {active: false, x: 0, y: 0};
  let reconnectDelay = 250;
  let frames = 0;
  let lastFpsAt = performance.now();
  let displayedFps = 0;
  let inputSequence = 0;
  let credential = '';
  let authenticated = false;

  function send(type, extra = {}) {
    if (authenticated && socket && socket.readyState === WebSocket.OPEN)
      socket.send(JSON.stringify({v: 2, type, ...extra}));
  }

  function sendInput() { send('input', {seq: ++inputSequence, buttons, hotkeys, touch}); }

  function recalculateButtons() {
    let value = 0;
    let hotkeyValue = 0;
    for (const pointer of pointers.values()) {
      value |= pointer.bits || 0;
      hotkeyValue |= pointer.hotkeys || 0;
    }
    buttons = value;
    hotkeys = hotkeyValue;
    document.querySelectorAll('[data-button], [data-hotkey]').forEach(el => {
      const active = el.dataset.button !== undefined
        ? (buttons & (1 << Number(el.dataset.button))) !== 0
        : (hotkeys & (1 << Number(el.dataset.hotkey))) !== 0;
      el.classList.toggle('active', active);
    });
    sendInput();
  }

  function bindButton(button) {
    if (button.dataset.inputBound) return;
    button.dataset.inputBound = '1';
    button.addEventListener('pointerdown', event => {
      event.preventDefault();
      button.setPointerCapture(event.pointerId);
      pointers.set(event.pointerId, {
        bits: button.dataset.button === undefined ? 0 : 1 << Number(button.dataset.button),
        hotkeys: button.dataset.hotkey === undefined ? 0 : 1 << Number(button.dataset.hotkey)
      });
      recalculateButtons();
    });
    const release = event => {
      if (pointers.delete(event.pointerId)) recalculateButtons();
    };
    button.addEventListener('pointerup', release);
    button.addEventListener('pointercancel', release);
    button.addEventListener('lostpointercapture', release);
  }
  document.querySelectorAll('[data-button]').forEach(bindButton);

  function applyLayout(layout) {
    if (!layout || (layout.version !== 1 && layout.version !== 2) || !Array.isArray(layout.items)) return;
    hud.hidden = layout.showHud === false;
    document.querySelectorAll('[data-layout-custom]').forEach(element => element.remove());
    document.querySelectorAll('[data-layout-id]').forEach(element => { element.hidden = true; });
    for (const item of layout.items) {
      if (!item || typeof item.id !== 'string') continue;
      let element = document.querySelector(`[data-layout-id="${CSS.escape(item.id)}"]`);
      if (!element && item.kind === 'button' && Number.isInteger(item.hotkey)) {
        element = document.createElement('button');
        element.className = 'custom';
        element.dataset.layoutCustom = '1';
        element.dataset.layoutId = item.id;
        element.dataset.hotkey = String(item.hotkey);
        element.textContent = String(item.label || 'Action').slice(0, 24);
        element.setAttribute('aria-label', element.textContent);
        document.getElementById('controller').appendChild(element);
        bindButton(element);
      }
      if (!element) continue;
      element.hidden = false;
      if (item.id === 'dpad') element.classList.toggle('analog', item.appearance === 'analog');
      if (item.kind === 'button' && typeof item.label === 'string') {
        element.textContent = item.label.slice(0, 24);
        element.setAttribute('aria-label', element.textContent);
      }
      for (const [property, value] of [['left', item.x], ['top', item.y], ['width', item.w], ['height', item.h]]) {
        if ((item.kind === 'screen' || item.kind === 'directional' || item.kind === 'face') && property === 'height') continue;
        if (typeof value === 'number' && Number.isFinite(value)) element.style[property] = `${value * 100}%`;
      }
      if (item.kind === 'screen') {
        element.style.aspectRatio = '4 / 3';
        element.style.height = 'auto';
      } else if (item.kind === 'directional' || item.kind === 'face') {
        element.style.aspectRatio = '1 / 1';
        element.style.height = 'auto';
      }
    }
  }

  function dpadBits(event) {
    const rect = dpad.getBoundingClientRect();
    const x = (event.clientX - rect.left) / rect.width - 0.5;
    const y = (event.clientY - rect.top) / rect.height - 0.5;
    if (dpad.classList.contains('analog')) {
      if (Math.hypot(x, y) < 0.075) return 0;
      const directions = [
        1 << 4,
        (1 << 4) | (1 << 7),
        1 << 7,
        (1 << 7) | (1 << 5),
        1 << 5,
        (1 << 5) | (1 << 6),
        1 << 6,
        (1 << 6) | (1 << 4)
      ];
      const octant = (Math.round(Math.atan2(y, x) / (Math.PI / 4)) + 8) % 8;
      return directions[octant];
    }
    let value = 0;
    if (Math.abs(x) > 0.12) value |= 1 << (x > 0 ? 4 : 5);
    if (Math.abs(y) > 0.12) value |= 1 << (y > 0 ? 7 : 6);
    return value;
  }
  function updateDirectionalVisual(event) {
    if (!dpad.classList.contains('analog')) return;
    const rect = dpad.getBoundingClientRect();
    let x = (event.clientX - rect.left) / rect.width - 0.5;
    let y = (event.clientY - rect.top) / rect.height - 0.5;
    const length = Math.hypot(x, y);
    if (length > 0.45) { x *= 0.45 / length; y *= 0.45 / length; }
    dpad.style.setProperty('--stick-x', `${x * rect.width * 0.42}px`);
    dpad.style.setProperty('--stick-y', `${y * rect.height * 0.42}px`);
  }
  dpad.addEventListener('pointerdown', event => {
    event.preventDefault();
    dpad.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId, {bits: dpadBits(event), dpad: true});
    updateDirectionalVisual(event);
    dpad.classList.add('active');
    recalculateButtons();
  });
  dpad.addEventListener('pointermove', event => {
    const value = pointers.get(event.pointerId);
    if (!value || !value.dpad) return;
    value.bits = dpadBits(event);
    updateDirectionalVisual(event);
    recalculateButtons();
  });
  const releaseDpad = event => {
    if (pointers.delete(event.pointerId)) recalculateButtons();
    dpad.classList.remove('active');
    dpad.style.setProperty('--stick-x', '0px');
    dpad.style.setProperty('--stick-y', '0px');
  };
  dpad.addEventListener('pointerup', releaseDpad);
  dpad.addEventListener('pointercancel', releaseDpad);
  dpad.addEventListener('lostpointercapture', releaseDpad);

  function updateTouch(event, active) {
    const rect = canvas.getBoundingClientRect();
    const x = Math.floor((event.clientX - rect.left) * 256 / rect.width);
    const y = Math.floor((event.clientY - rect.top) * 192 / rect.height);
    touch = {active, x: Math.max(0, Math.min(255, x)), y: Math.max(0, Math.min(191, y))};
    sendInput();
  }
  canvas.addEventListener('pointerdown', event => {
    event.preventDefault();
    canvas.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId, {touch: true});
    updateTouch(event, true);
  });
  canvas.addEventListener('pointermove', event => {
    if (pointers.get(event.pointerId)?.touch) updateTouch(event, true);
  });
  const releaseTouch = event => {
    if (!pointers.get(event.pointerId)?.touch) return;
    pointers.delete(event.pointerId);
    updateTouch(event, false);
  };
  canvas.addEventListener('pointerup', releaseTouch);
  canvas.addEventListener('pointercancel', releaseTouch);
  canvas.addEventListener('lostpointercapture', releaseTouch);

  function releaseAll() {
    pointers.clear();
    buttons = 0;
    hotkeys = 0;
    touch = {active: false, x: 0, y: 0};
    recalculateButtons();
    send('visibility', {hidden: true});
  }

  async function displayFrame(buffer) {
    if (buffer.byteLength < 24) return;
    const view = new DataView(buffer);
    if (view.getUint32(0, false) !== 0x574d4632 || view.getUint8(20) !== 1) return;
    const sequence = view.getUint32(4, true);
    try {
      const bitmap = await createImageBitmap(new Blob([buffer.slice(24)], {type: 'image/jpeg'}));
      context.imageSmoothingEnabled = false;
      context.drawImage(bitmap, 0, 0, 256, 192);
      bitmap.close();
      frames++;
      const now = performance.now();
      if (now - lastFpsAt >= 1000) {
        displayedFps = frames * 1000 / (now - lastFpsAt);
        frames = 0;
        lastFpsAt = now;
        metrics.textContent = `${displayedFps.toFixed(1)} FPS · frame ${sequence}`;
      }
      send('frameAck', {seq: sequence});
    } catch (error) {
      metrics.textContent = `Decode error: ${error.message}`;
      send('frameAck', {seq: sequence});
    }
  }

  function connect() {
    if (!credential) {
      pairing.hidden = false;
      status.textContent = 'Enter pairing code';
      return;
    }
    const url = `ws://${location.host}/bridge`;
    status.textContent = 'Connecting paired session…';
    authenticated = false;
    socket = new WebSocket(url);
    socket.binaryType = 'arraybuffer';
    socket.onopen = () => {
      reconnectDelay = 250;
      status.textContent = 'Authenticating…';
      socket.send(JSON.stringify({v: 2, type: 'auth', credential}));
    };
    socket.onmessage = event => {
      if (typeof event.data !== 'string') { displayFrame(event.data); return; }
      try {
        const message = JSON.parse(event.data);
        if (message.v !== 2) return;
        if (message.type === 'hello') {
          authenticated = true;
          pairing.hidden = true;
          status.textContent = 'Connected';
          sendInput();
        }
        if ((message.type === 'hello' || message.type === 'layout') && message.layout)
          applyLayout(message.layout);
        if (message.type === 'ping') send('pong', {sent: message.sent});
      } catch (_) {}
    };
    socket.onclose = event => {
      releaseAll();
      authenticated = false;
      if (event.reason === 'Authentication failed' || event.reason === 'Pairing changed') {
        credential = '';
        sessionStorage.removeItem('widemelonPairing');
        pairing.hidden = false;
        status.textContent = event.reason === 'Pairing changed' ? 'Pairing code changed' : 'Pairing failed';
        pairingCode.focus();
      } else {
        status.textContent = 'Disconnected; retrying…';
        setTimeout(connect, reconnectDelay);
        reconnectDelay = Math.min(5000, reconnectDelay * 2);
      }
    };
    socket.onerror = () => { status.textContent = 'Connection error'; };
  }

  document.addEventListener('visibilitychange', () => {
    if (document.hidden) releaseAll();
    else sendInput();
  });
  window.addEventListener('blur', releaseAll);
  window.addEventListener('contextmenu', event => event.preventDefault());
  setInterval(sendInput, 200);
  const fullscreenButton = document.getElementById('fullscreen');
  fullscreenButton.addEventListener('click', () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else document.documentElement.requestFullscreen?.();
  });
  document.addEventListener('fullscreenchange', () => {
    const label = document.fullscreenElement ? 'Exit full screen' : 'Enter full screen';
    fullscreenButton.setAttribute('aria-label', label);
    fullscreenButton.title = label;
  });
  const fragment = new URLSearchParams(location.hash.slice(1)).get('pair') || '';
  if (/^[A-Za-z0-9_-]{43}$/.test(fragment)) {
    credential = fragment;
    sessionStorage.setItem('widemelonPairing', credential);
    history.replaceState(null, '', `${location.pathname}${location.search}`);
  } else {
    credential = sessionStorage.getItem('widemelonPairing') || '';
  }
  pairingForm.addEventListener('submit', event => {
    event.preventDefault();
    const code = pairingCode.value.replace(/\D/g, '');
    if (!/^\d{10}$/.test(code)) return;
    credential = code;
    sessionStorage.setItem('widemelonPairing', credential);
    pairingCode.value = '';
    pairing.hidden = true;
    connect();
  });
  connect();
})();
