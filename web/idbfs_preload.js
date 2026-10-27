// Mounts an IndexedDB-backed filesystem at /persist and loads any previously
// persisted saves (scores.txt / savegame.txt) BEFORE main() runs.
//
// addRunDependency holds main() until the initial FS.syncfs(true) finishes, so
// the C-side scores_charger() / charger_partie() see prior-session data.  The
// reverse direction (persisting after a write) is done in C via FS.syncfs(false)
// — see PERSIST_SYNC() in background.c.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  try { FS.mkdir('/persist'); } catch (e) { /* already exists */ }
  FS.mount(IDBFS, {}, '/persist');
  addRunDependency('idbfs-syncfs');
  FS.syncfs(true, function (err) {
    if (err) console.warn('[idbfs] initial load failed:', err);
    removeRunDependency('idbfs-syncfs');
  });
});
