// Runs before the app starts. The browser has no disk, so the progress folder
// (~/.cadet-prep, see AppPaths.h) is kept in the browser's own storage (IndexedDB):
// loaded before main() runs, and written back whenever the app saves a file.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  var dir = '/home/web_user/.cadet-prep';
  FS.mkdirTree(dir);
  FS.mount(IDBFS, { autoPersist: true }, dir);
  addRunDependency('progress');
  FS.syncfs(true, function (err) {
    if (err) console.warn('Could not load saved progress:', err);
    removeRunDependency('progress');
  });
});
