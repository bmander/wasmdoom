import { test, expect } from '@playwright/test';
import { readFileSync, writeFileSync } from 'node:fs';

async function start(page) {
  await page.getByRole('button', {name: 'PLAY DOOM'}).click();
  await expect(page.locator('#status')).not.toHaveText('LOADING ENGINE', { timeout: 30000 });
  if (await page.locator('#error').isVisible()) throw new Error(await page.locator('#error').textContent());
  await expect(page.locator('#status')).toHaveText('RUNNING', { timeout: 30000 });
}
async function frame(page) {
  return page.locator('canvas').evaluate(canvas => canvas.toDataURL());
}
async function enterLevel(page) {
  await page.locator('canvas').focus();
  await page.keyboard.press('Escape');
  await page.waitForTimeout(200);
  await page.keyboard.press('Enter');
  await page.waitForTimeout(200);
  await page.keyboard.press('Enter');
  await page.waitForTimeout(200);
  await page.keyboard.press('Enter');
  await page.waitForTimeout(2000);
}

test('compiled engine renders and plays, with pause, sound, fullscreen and focus handling', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('/');
  await page.screenshot({path: 'test-results/landing.png', fullPage: true});
  const blank = await frame(page);
  await start(page);
  await expect.poll(() => frame(page)).not.toBe(blank);
  const title = await frame(page);
  await enterLevel(page);
  expect(await frame(page)).not.toBe(title);
  const before = await frame(page);
  await page.keyboard.down('ArrowUp');
  await page.waitForTimeout(600);
  await page.keyboard.up('ArrowUp');
  expect(await frame(page)).not.toBe(before);
  await page.keyboard.press('Control');
  await page.getByRole('button', {name: 'Pause', exact: false}).click();
  await expect(page.locator('#status')).toHaveText('PAUSED');
  await page.waitForTimeout(200);
  const frozen = await frame(page);
  await page.waitForTimeout(300);
  expect(await frame(page)).toBe(frozen);
  await page.locator('#play').click();
  await expect(page.locator('#status')).toHaveText('RUNNING');
  await page.getByRole('button', {name: 'Sound on'}).click();
  await expect(page.locator('#sound')).toHaveText('Sound off');
  await page.getByRole('button', {name: 'Fullscreen'}).click();
  await expect.poll(() => page.evaluate(() => !!document.fullscreenElement)).toBeTruthy();
  await page.evaluate(() => document.exitFullscreen());
  await page.screenshot({path: 'test-results/playing.png', fullPage: true});
  await page.evaluate(() => window.dispatchEvent(new Event('blur')));
  await expect(page.locator('#status')).toHaveText('PAUSED');
  expect(errors).toEqual([]);
});

test('validates local WADs and starts a supplied IWAD without fetching bundled data', async ({page}) => {
  await page.goto('/');
  await page.locator('#wad').setInputFiles({name:'bad.wad', mimeType:'application/octet-stream', buffer:Buffer.from('bad')});
  await expect(page.getByRole('alert')).toContainText('too small');
  await page.route('**/assets/doom1.wad', route => route.abort());
  await page.locator('#wad').setInputFiles('public/assets/doom1.wad');
  await expect(page.locator('#file-note')).toContainText('stays on your device');
  await start(page);
});

test('download failure is visible and offers retry', async ({page}) => {
  await page.route('**/assets/doom1.wad', route => route.fulfill({status:503, body:'unavailable'}));
  await page.goto('/');
  await page.getByRole('button', {name:'PLAY DOOM'}).click();
  await expect(page.getByRole('alert')).toContainText('503');
  await expect(page.getByRole('button', {name:'RELOAD & RETRY'})).toBeEnabled();
});

test('layout fits narrow screens', async ({page}) => {
  await page.setViewportSize({width:390,height:844});
  await page.goto('/');
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(390);
  await page.screenshot({path:'test-results/mobile.png',fullPage:true});
});

async function saveSlot(page, name) {
  await page.locator('#save').click();
  await page.keyboard.press('Enter');
  await page.waitForTimeout(150);
  await page.keyboard.type(name, {delay:60});
  await page.keyboard.press('Enter');
  await expect(page.locator('#save-state')).toHaveText('Saved in this browser');
}

async function storedSlots(page) {
  return page.evaluate(() => new Promise((resolve, reject) => {
    const open = indexedDB.open('wasmdoom-saves', 1);
    open.onerror = () => reject(open.error);
    open.onsuccess = () => {
      const db = open.result;
      const tx = db.transaction('slots', 'readonly');
      const request = tx.objectStore('slots').getAll();
      request.onsuccess = () => resolve(request.result.map(bytes => ({
        description: new TextDecoder().decode(bytes.slice(0, 24)).replace(/\0.*$/, ''),
        bytes: Array.from(bytes),
      })));
      tx.oncomplete = () => db.close();
    };
  }));
}

// Static walls at the top of the view exclude the animated weapon and HUD.
async function viewSignature(page) {
  return page.locator('canvas').evaluate(canvas => {
    const pixels = canvas.getContext('2d').getImageData(160, 40, 320, 80).data;
    let hash = 2166136261;
    for (const byte of pixels) hash = Math.imul(hash ^ byte, 16777619);
    return hash >>> 0;
  });
}

test('mouse capture turns and fires; releasing capture pauses and clears input', async ({page}) => {
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  await page.goto('/');
  await start(page);
  await enterLevel(page);
  await page.locator('canvas').click();
  await expect.poll(() => page.evaluate(() => !!document.pointerLockElement)).toBeTruthy();
  const before = await viewSignature(page);
  await page.mouse.move(800, 500, {steps:8});
  await expect.poll(() => viewSignature(page)).not.toBe(before);
  await page.mouse.down();
  await page.waitForTimeout(700);
  await page.mouse.up();
  await page.keyboard.press('Escape');
  // CDP's Escape does not always invoke Chrome's native unlock gesture.
  await page.evaluate(() => { if (document.pointerLockElement) document.exitPointerLock(); });
  await expect(page.locator('#status')).toHaveText('PAUSED');
  await page.locator('#play').click();
  await expect(page.locator('#status')).toHaveText('RUNNING');
  const direction = await viewSignature(page);
  await page.mouse.move(100, 100);
  await page.waitForTimeout(150);
  expect(await viewSignature(page)).toBe(direction);
  await saveSlot(page, 'MOUSE WASD');
  const [saved] = await storedSlots(page);
  expect(saved.description).toBe('MOUSE WASD');
  await page.screenshot({path:'test-results/mouse-gameplay.png',fullPage:true});
  expect(errors).toEqual([]);
});

test('a real save survives closing the page, loads its position, and stays isolated by WAD', async ({page, context}) => {
  await page.goto('/');
  await start(page);
  await enterLevel(page);
  await page.keyboard.down('ArrowRight');
  await page.waitForTimeout(450);
  await page.keyboard.up('ArrowRight');
  await page.waitForTimeout(200);
  const savedView = await viewSignature(page);
  await saveSlot(page, 'WASD E F');
  const [slot] = await storedSlots(page);
  expect(slot.description).toBe('WASD E F');
  expect(slot.bytes.length).toBeGreaterThan(10000);
  await page.close();

  const restored = await context.newPage();
  await restored.goto('/');
  await start(restored);
  await expect(restored.locator('#save-state')).toHaveText('1 saved slot restored');
  await restored.locator('#load').click();
  await restored.keyboard.press('Enter');
  await expect.poll(() => viewSignature(restored), {timeout:10000}).toBe(savedView);
  await restored.screenshot({path:'test-results/restored-save.png',fullPage:true});

  const other = Buffer.from(readFileSync('public/assets/doom1.wad'));
  // Change one palette byte: still a valid playable IWAD, but a different identity.
  other[100] ^= 1;
  await restored.reload();
  await restored.locator('#wad').setInputFiles({name:'another.wad', mimeType:'application/octet-stream',buffer:other});
  await expect(restored.locator('#file-note')).toContainText('another.wad');
  await start(restored);
  await expect(restored.locator('#save-state')).toHaveText('Saves stay in this browser');
  expect(await storedSlots(restored)).toHaveLength(1);
});

test('storage failure keeps the game playable and warns that saves are temporary', async ({page}) => {
  await page.addInitScript(() => { indexedDB.open = () => { throw new DOMException('Denied', 'SecurityError'); }; });
  await page.goto('/');
  await start(page);
  await expect(page.locator('#save-state')).toContainText('storage unavailable');
});

test('an aborted save transaction is reported and can be retried', async ({page}) => {
  await page.goto('/');
  await start(page);
  await enterLevel(page);
  await page.evaluate(() => {
    const original = IDBDatabase.prototype.transaction;
    window.rejectSaves = true;
    IDBDatabase.prototype.transaction = function (...args) {
      const tx = original.apply(this, args);
      if (args[1] === 'readwrite' && window.rejectSaves) queueMicrotask(() => tx.abort());
      return tx;
    };
  });
  await page.locator('#save').click();
  await page.keyboard.press('Enter');
  await page.waitForTimeout(150);
  await page.keyboard.type('RETRY');
  await page.keyboard.press('Enter');
  await expect(page.locator('#save-state')).toContainText('Save not stored');
  await page.evaluate(() => { window.rejectSaves = false; });
  await page.locator('#retry-save').click();
  await expect(page.locator('#save-state')).toHaveText('Saved in this browser');
  expect((await storedSlots(page))[0].description).toBe('RETRY');
});

test('resolution modes render new detail, switch live, and preserve a cross-resolution save', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.addInitScript(() => {
    window.doomFrames = [];
    const draw = CanvasRenderingContext2D.prototype.putImageData;
    CanvasRenderingContext2D.prototype.putImageData = function (...args) {
      if (this.canvas.id === 'game') window.doomFrames.push(performance.now());
      return draw.apply(this, args);
    };
  });
  await page.goto('/');
  await page.locator('#resolution').selectOption('1');
  await start(page);
  await enterLevel(page);
  await saveSlot(page, 'RESOLUTION');
  const performanceByMode = {};
  for (const scale of [1, 2, 4]) {
    await page.locator('#resolution').selectOption(String(scale));
    await expect(page.locator('canvas')).toHaveAttribute('width', String(320 * scale));
    await expect(page.locator('canvas')).toHaveAttribute('height', String(200 * scale));
    await page.locator('canvas').focus();
    await page.waitForTimeout(300);
    const data = await page.locator('canvas').evaluate((canvas, scale) => {
      const ctx = canvas.getContext('2d');
      const pixels = new Uint32Array(ctx.getImageData(0, 0, canvas.width, canvas.height).data.buffer);
      let varied = 0, blocks = 0;
      // Each block would be a single color if this were a 320x200 upscale.
      for (let y = 20 * scale; y < 150 * scale; y += scale) {
        for (let x = 10 * scale; x < 310 * scale; x += scale) {
          const base = pixels[y * canvas.width + x];
          let changed = false;
          for (let dy = 0; dy < scale; dy++) for (let dx = 0; dx < scale; dx++) {
            if (pixels[(y + dy) * canvas.width + x + dx] !== base) changed = true;
          }
          blocks++;
          if (changed) varied++;
        }
      }
      return { varied: varied / blocks, image: canvas.toDataURL() };
    }, scale);
    if (scale > 1) expect(data.varied).toBeGreaterThan(0.1);
    writeFileSync(`test-results/native-${320 * scale}x${200 * scale}.png`, Buffer.from(data.image.split(',')[1], 'base64'));
    await page.evaluate(() => { window.doomFrames = []; });
    await page.waitForTimeout(1200);
    performanceByMode[scale] = await page.evaluate(() => {
      const times = window.doomFrames;
      return Math.round((times.length - 1) * 1000 / (times.at(-1) - times[0]));
    });
    expect(performanceByMode[scale]).toBeGreaterThan(10);
    // Exercise borders, full view, automap, and return to the normal HUD.
    await page.keyboard.press('Minus');
    await page.waitForTimeout(150);
    await page.keyboard.press('Equal');
    await page.keyboard.press('Equal');
    await page.waitForTimeout(150);
    await page.keyboard.press('Minus');
    await page.keyboard.press('Tab');
    await page.waitForTimeout(150);
    await page.keyboard.press('Tab');
    await expect(page.locator('#status')).toHaveText('RUNNING');
  }
  console.log('Rendered FPS by scale:', performanceByMode);
  await page.reload();
  await expect(page.locator('#resolution')).toHaveValue('4');
  await start(page);
  await expect(page.locator('canvas')).toHaveAttribute('width', '1280');
  await expect(page.locator('#save-state')).toHaveText('1 saved slot restored');
  await page.locator('#load').click();
  await page.keyboard.press('Enter');
  await page.waitForTimeout(1500);
  await expect(page.locator('#status')).toHaveText('RUNNING');
  await page.screenshot({path:'test-results/high-resolution-page.png',fullPage:true});
  // Changing while paused must defer safely until a complete engine tick.
  await page.locator('#pause').click();
  await page.locator('#resolution').selectOption('1');
  await expect(page.locator('#resolution-state')).toContainText('resume');
  await page.locator('#play').click();
  await expect(page.locator('canvas')).toHaveAttribute('width', '320');
  expect(errors).toEqual([]);
});

test('worthy adversaries toggles live, remembers its setting, and keeps saves compatible', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('/');
  const mod = page.getByRole('checkbox', {name:'Worthy adversaries'});
  await expect(mod).not.toBeChecked();
  await mod.check();
  await expect(page.locator('#worthy-state')).toHaveText('TACTICAL ENEMY AI');
  await start(page);
  await enterLevel(page);
  await page.keyboard.down('ArrowUp');
  await page.waitForTimeout(700);
  await page.keyboard.up('ArrowUp');
  // Gunfire wakes the normal audible enemies; exercise the tactical thinkers.
  await page.keyboard.down('Control');
  await page.waitForTimeout(2500);
  await page.keyboard.up('Control');
  await saveSlot(page, 'WORTHY');
  await mod.uncheck();
  await expect(page.locator('#worthy-state')).toHaveText('CLASSIC ENEMY AI');
  await expect(page.locator('#status')).toHaveText('RUNNING');
  await mod.check();
  await page.screenshot({path:'test-results/worthy-adversaries.png',fullPage:true});
  await page.reload();
  await expect(mod).toBeChecked();
  await start(page);
  await expect(page.locator('#save-state')).toHaveText('1 saved slot restored');
  await page.locator('#load').click();
  await page.keyboard.press('Enter');
  await page.waitForTimeout(1500);
  await expect(page.locator('#status')).toHaveText('RUNNING');
  await page.locator('#pause').click();
  await mod.uncheck();
  await page.locator('#play').click();
  await expect(page.locator('#status')).toHaveText('RUNNING');
  expect(errors).toEqual([]);
});
