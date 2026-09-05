import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { validateWad } from '../public/wad.js';

test('accepts the bundled real IWAD', () => {
  const bytes = readFileSync(new URL('../public/assets/doom1.wad', import.meta.url));
  assert.equal(validateWad(bytes), bytes);
});
test('rejects truncated containers, PWADs and corrupt directory entries', () => {
  assert.throws(() => validateWad(new Uint8Array(4)), /too small/);
  const wad = readFileSync(new URL('../public/assets/doom1.wad', import.meta.url));
  const pwad = Buffer.from(wad); pwad.write('PWAD');
  assert.throws(() => validateWad(pwad), /IWAD/);
  assert.throws(() => validateWad(wad.subarray(0, 100)), /directory/);
  const corrupt = Buffer.from(wad);
  corrupt.writeInt32LE(-1, corrupt.readInt32LE(8));
  assert.throws(() => validateWad(corrupt), /invalid data/);
});
