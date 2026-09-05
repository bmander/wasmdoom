// Basic container validation; the DOOM engine validates the game content.
export function validateWad(bytes) {
  if (bytes.byteLength < 12) throw new Error('This file is too small to be a WAD.');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const magic = String.fromCharCode(...bytes.subarray(0, 4));
  if (magic !== 'IWAD') throw new Error('Choose a DOOM or DOOM II IWAD. Add-on PWADs need a base game and are not supported here.');
  const count = view.getInt32(4, true);
  const directory = view.getInt32(8, true);
  if (count < 1 || directory < 12 || directory + count * 16 > bytes.byteLength) throw new Error('The WAD directory is damaged or incomplete.');
  const names = new Set();
  for (let i = 0; i < count; i++) {
    const entry = directory + i * 16;
    const offset = view.getInt32(entry, true);
    const size = view.getInt32(entry + 4, true);
    if (offset < 0 || size < 0 || offset + size > bytes.byteLength) throw new Error('The WAD contains an invalid data entry.');
    names.add(String.fromCharCode(...bytes.subarray(entry + 8, entry + 16)).replace(/\0.*$/, ''));
  }
  if (!names.has('PLAYPAL') || (!names.has('E1M1') && !names.has('MAP01'))) throw new Error('This IWAD does not contain a supported DOOM game.');
  return bytes;
}
