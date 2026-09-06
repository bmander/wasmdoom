import { validateWad } from './wad.js';
import { connectSaves } from './saves.js';

const $ = (id) => document.getElementById(id);
let engine, selectedWad, running = false, paused = false, muted = false, failed = false;
let saves, mouseButtons = 0, mouseLocked = false;
try {
  const saved = localStorage.getItem('doom-resolution');
  if (['1', '2', '4'].includes(saved)) $('resolution').value = saved;
} catch { /* Resolution preferences are optional when storage is unavailable. */ }
try { $('worthy').checked = localStorage.getItem('doom-worthy-adversaries') === 'true'; } catch { /* Optional preference. */ }
function updateWorthy() {
  const enabled = $('worthy').checked;
  if (engine && !failed) engine._doom_worthy(Number(enabled));
  $('worthy-state').textContent = enabled ? 'NEURAL ENEMY AI' : 'CLASSIC ENEMY AI';
  $('worthy').closest('.adversaries').classList.toggle('enabled', enabled);
}
updateWorthy();
$('worthy').addEventListener('change', () => {
  try { localStorage.setItem('doom-worthy-adversaries', String($('worthy').checked)); } catch { /* Optional preference. */ }
  updateWorthy();
});
const held = new Map();
const keymap = {ArrowUp:173, KeyW:173, ArrowDown:175, KeyS:175, ArrowLeft:172, ArrowRight:174, KeyA:160, KeyD:161, ControlLeft:163, ControlRight:163, KeyF:163, Space:162, KeyE:162, ShiftLeft:182, ShiftRight:182, AltLeft:184, AltRight:184, Enter:13, Escape:27, Tab:9, Backspace:127, Minus:45, Equal:61};
for (let i = 0; i < 10; i++) keymap[`Digit${i}`] = 48 + i;
Object.assign(keymap, { F2:188, F3:189, F6:192, F9:195 });

function error(message) { $('error').textContent = message; $('error').hidden = false; }
function releaseKeys() {
  for (const key of new Set(held.values())) engine?._doom_key(key, 0);
  held.clear();
  mouseButtons = 0;
  engine?._doom_release_mouse();
}
function setPaused(value) {
  if (!running || failed) return;
  releaseKeys();
  paused = value;
  if (value && document.pointerLockElement === $('game')) document.exitPointerLock();
  engine._doom_pause(Number(value));
  $('overlay').hidden = !value;
  $('overlay-caption').textContent = 'TAKE A BREATHER';
  $('overlay-description').textContent = 'Hell can wait.';
  $('overlay-hint').textContent = 'Click resume to get back in the game';
  $('play').innerHTML = 'RESUME <span>↗</span>';
  $('pause').textContent = value ? '▶ Resume' : 'Ⅱ Pause';
  $('status').textContent = value ? 'PAUSED' : 'RUNNING';
  if (!value) $('game').focus();
}
function fail(message) {
  releaseKeys();
  if (document.pointerLockElement === $('game')) document.exitPointerLock();
  failed = true;
  running = false;
  error(message);
  $('status').textContent = 'COULD NOT START';
  $('overlay').hidden = false;
  $('overlay-description').textContent = 'Something interrupted the game.';
  $('play').textContent = 'RELOAD & RETRY ↗';
  $('play').disabled = false;
  $('pause').disabled = true;
  $('sound').disabled = true;
  $('save').disabled = $('load').disabled = true;
}

$('wad').addEventListener('change', async (event) => {
  const file = event.target.files[0];
  if (!file) return;
  $('play').disabled = true;
  try {
    if (file.size > 128 * 1024 * 1024) throw new Error('Choose an IWAD smaller than 128 MB.');
    selectedWad = validateWad(new Uint8Array(await file.arrayBuffer()));
    $('wad-name').textContent = file.name.toUpperCase();
    $('overlay-caption').textContent = 'YOUR WAD / READY';
    $('file-note').textContent = `${file.name} · ${(file.size / 1048576).toFixed(1)} MB · stays on your device`;
    $('error').hidden = true;
  } catch (cause) {
    selectedWad = undefined;
    $('wad-name').textContent = 'DOOM SHAREWARE';
    $('overlay-caption').textContent = 'EPISODE 01 / SHAREWARE';
    $('file-note').textContent = 'Using the bundled shareware episode.';
    error(cause.message);
  } finally { $('play').disabled = false; }
});

$('play').addEventListener('click', async () => {
  if (failed) return location.reload();
  if (running) return setPaused(false);
  $('play').disabled = true;
  $('wad').disabled = true;
  $('play').textContent = 'LOADING…';
  $('status').textContent = 'LOADING ENGINE';
  $('error').hidden = true;
  try {
    const [{ default: createDoom }, bytes] = await Promise.all([
      import('./engine/doom.js'),
      selectedWad || fetch('./assets/doom1.wad').then(async (response) => {
        if (!response.ok) throw new Error(`Game data download failed (${response.status}).`);
        return validateWad(new Uint8Array(await response.arrayBuffer()));
      }),
    ]);
    engine = await createDoom({
      canvas: $('game'), noInitialRun: true,
      print: (line) => console.info('[DOOM]', line),
      printErr: (line) => console.warn('[DOOM]', line),
      onAbort: (reason) => fail(`The engine stopped: ${reason}`),
      onExit: () => fail('DOOM has exited. Reload the page to play again.'),
      onFrame: (width, height) => {
        if (width === Number($('resolution').value) * 320) {
          $('resolution-state').textContent = `${width} × ${height} native 3D · Original proportions`;
        }
        if (!running && !failed) {
          running = true;
          $('overlay').hidden = true;
          $('play').disabled = false;
          $('pause').disabled = false;
          $('sound').disabled = false;
          $('save').disabled = $('load').disabled = false;
          $('status').textContent = 'RUNNING';
          $('status-light').classList.add('live');
          $('session-label').textContent = 'WELCOME BACK, MARINE.';
          $('game').focus();
        }
      },
    });
    engine.FS.writeFile('/game.wad', bytes);
    engine._doom_resolution(Number($('resolution').value));
    updateWorthy();
    saves = await connectSaves(engine.FS, bytes, (message, retry) => {
      $('save-state').textContent = message;
      $('retry-save').hidden = !retry;
    });
    engine.callMain(['-iwad', '/game.wad', '-nomusic']);
  } catch (cause) { fail(`Could not load DOOM. ${cause.message || cause}`); }
});

window.addEventListener('keydown', (event) => {
  if (!running || paused || document.activeElement !== $('game') || event.metaKey) return;
  // Escape belongs to the browser while the mouse is captured. Unlock pauses.
  if (event.code === 'Escape' && mouseLocked) return;
  // Movement aliases must not turn a save name such as "WASD" into arrow keys.
  const typing = engine._doom_text_input();
  const key = typing && event.key.length === 1 && /^[\x20-\x7e]$/.test(event.key)
    ? event.key.toLowerCase().charCodeAt(0) : keymap[event.code];
  if (key === undefined) return;
  event.preventDefault();
  if (!held.has(event.code)) {
    if (![...held.values()].includes(key)) engine._doom_key(key, 1);
    held.set(event.code, key);
  }
});
window.addEventListener('keyup', (event) => {
  if (!held.has(event.code)) return;
  event.preventDefault();
  const key = held.get(event.code);
  held.delete(event.code);
  if (![...held.values()].includes(key)) engine._doom_key(key, 0);
});
window.addEventListener('blur', () => setPaused(true));
document.addEventListener('visibilitychange', () => { if (document.hidden) setPaused(true); });
$('game').addEventListener('blur', releaseKeys);
$('game').addEventListener('click', async () => {
  if (!running || paused || failed || mouseLocked) return;
  try {
    await $('game').requestPointerLock();
  } catch { mouseUnavailable(); }
});
function mouseUnavailable() {
  $('mouse-hint').textContent = 'Mouse capture unavailable. Click to retry, or use keyboard controls.';
}
document.addEventListener('pointerlockerror', mouseUnavailable);
document.addEventListener('pointerlockchange', () => {
  const wasLocked = mouseLocked;
  mouseLocked = document.pointerLockElement === $('game');
  if (mouseLocked && (!running || paused || failed)) {
    document.exitPointerLock();
    return;
  }
  $('mouse-hint').textContent = mouseLocked
    ? 'Mouse captured · Left click: fire · Right click: use · Esc: release & pause'
    : 'Click the game to capture your mouse. Esc releases it and pauses.';
  releaseKeys();
  if (wasLocked && !mouseLocked && !paused) setPaused(true);
});
document.addEventListener('mousemove', (event) => {
  if (!mouseLocked || !running || paused || failed) return;
  const dx = Math.max(-1024, Math.min(1024, event.movementX));
  engine._doom_mouse(Math.round(dx * Number($('sensitivity').value)), mouseButtons);
});
document.addEventListener('mousedown', (event) => {
  if (!mouseLocked || !running || paused || failed || ![0, 2].includes(event.button)) return;
  event.preventDefault();
  mouseButtons |= event.button === 0 ? 1 : 2;
  engine._doom_mouse(0, mouseButtons);
});
document.addEventListener('mouseup', (event) => {
  if (![0, 2].includes(event.button)) return;
  mouseButtons &= ~(event.button === 0 ? 1 : 2);
  if (mouseLocked && running && !failed) engine._doom_mouse(0, mouseButtons);
});
$('game').addEventListener('contextmenu', event => event.preventDefault());
$('sensitivity').addEventListener('input', () => {
  $('sensitivity-value').value = $('sensitivity').value;
});
$('resolution').addEventListener('change', () => {
  const scale = Number($('resolution').value);
  try { localStorage.setItem('doom-resolution', String(scale)); } catch { /* Optional preference. */ }
  if (engine && !failed) engine._doom_resolution(scale);
  $('resolution-state').textContent = paused ? 'Resolution will change when you resume.' : `${scale * 320} × ${scale * 200} native 3D · Original proportions`;
});
function openGameMenu(key) {
  if (!running || failed) return;
  if (paused) setPaused(false);
  releaseKeys();
  $('game').focus();
  engine._doom_key(key, 1);
  engine._doom_key(key, 0);
}
$('save').addEventListener('click', () => openGameMenu(keymap.F2));
$('load').addEventListener('click', () => openGameMenu(keymap.F3));
$('retry-save').addEventListener('click', () => saves?.retry());
window.addEventListener('beforeunload', event => {
  if (saves?.hasPending()) {
    event.preventDefault();
    event.returnValue = '';
  }
});
$('pause').addEventListener('click', () => setPaused(!paused));
$('sound').addEventListener('click', () => {
  muted = !muted;
  engine._doom_mute(Number(muted));
  $('sound').textContent = muted ? 'Sound off' : 'Sound on';
  $('sound').setAttribute('aria-pressed', String(muted));
  if (!paused) $('game').focus();
});
$('fullscreen').addEventListener('click', async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await $('viewport').requestFullscreen();
    if (running && !paused) $('game').focus();
  } catch { error('Fullscreen is unavailable in this browser. You can still play in the page.'); }
});
