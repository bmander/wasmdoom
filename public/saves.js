// Save only completed DOOM slots, immediately after the engine's atomic rename.
// Separate records per WAD and slot keep other games/tabs' slots intact.
const DB_NAME = 'wasmdoom-saves';
const STORE = 'slots';
const SAVE_DIR = '/.savegame';
const slotName = /^doomsav[0-5]\.dsg$/;

export async function connectSaves(FS, wad, report) {
  const pending = new Map();
  let db, writing = false, available = false;
  let wadId;
  FS.mkdirTree(SAVE_DIR);

  function flush() {
    if (!available || writing || pending.size === 0) return;
    writing = true;
    const batch = new Map(pending);
    report('Saving…', false);
    const failure = () => {
      writing = false;
      report('Save not stored. Keep this tab open and retry.', true);
    };
    try {
      const transaction = db.transaction(STORE, 'readwrite');
      const store = transaction.objectStore(STORE);
      for (const [name, bytes] of batch) store.put(bytes, `${wadId}/${name}`);
      transaction.oncomplete = () => {
        writing = false;
        for (const [name, bytes] of batch) {
          if (pending.get(name) === bytes) pending.delete(name);
        }
        if (pending.size) flush();
        else report('Saved in this browser', false);
      };
      transaction.onabort = failure;
      transaction.onerror = () => {}; // onabort reports the transaction failure.
    } catch { failure(); }
  }

  try {
    report('Checking saved games…', false);
    const digest = await crypto.subtle.digest('SHA-256', wad);
    wadId = [...new Uint8Array(digest)].map(byte => byte.toString(16).padStart(2, '0')).join('');
    db = await new Promise((resolve, reject) => {
      const request = indexedDB.open(DB_NAME, 1);
      let cancelled = false;
      request.onupgradeneeded = () => request.result.createObjectStore(STORE);
      request.onsuccess = () => { if (cancelled) request.result.close(); else resolve(request.result); };
      request.onerror = () => reject(request.error);
      request.onblocked = () => { cancelled = true; reject(new Error('Storage is blocked by another tab.')); };
    });
    db.onversionchange = () => {
      available = false;
      db.close();
      report('Save storage changed. Reload before continuing.', false);
    };
    const slots = await new Promise((resolve, reject) => {
      const transaction = db.transaction(STORE, 'readonly');
      const loaded = [];
      for (let slot = 0; slot < 6; slot++) {
        const name = `doomsav${slot}.dsg`;
        const request = transaction.objectStore(STORE).get(`${wadId}/${name}`);
        request.onsuccess = () => { if (request.result) loaded.push([name, request.result]); };
      }
      transaction.oncomplete = () => resolve(loaded);
      transaction.onabort = () => reject(transaction.error);
    });
    for (const [name, bytes] of slots) FS.writeFile(`${SAVE_DIR}/${name}`, bytes);
    available = true;
    report(slots.length ? `${slots.length} saved ${slots.length === 1 ? 'slot' : 'slots'} restored` : 'Saves stay in this browser', false);
  } catch {
    db?.close();
    report('Browser storage unavailable. Saves last only for this session.', false);
  }

  const rename = FS.rename;
  FS.rename = function (from, to) {
    const result = rename.call(FS, from, to);
    const path = FS.lookupPath(to).path;
    const name = path.slice(SAVE_DIR.length + 1);
    if (path.startsWith(`${SAVE_DIR}/`) && slotName.test(name)) {
      pending.set(name, FS.readFile(path));
      if (available) flush();
      else report('Save is only in memory. Browser storage is unavailable.', false);
    }
    return result;
  };

  return { retry: flush, hasPending: () => pending.size > 0 };
}
