// Runs before the app starts.
//
// Saving: the browser has no disk, so the progress folder (~/.cadet-prep, see
// AppPaths.h) is kept in the browser's own storage (IndexedDB): loaded before
// main() runs, and written back whenever the app saves a file.
//
// Login: when the site was built with an account server (config.js), the study
// page only opens while signed in. login.html leaves the new sign-in in
// localStorage; it's moved here into afoqt_account.json, where the app's online
// sync (CloudSync) reads it. No sign-in: back to the login page.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  var dir = '/home/web_user/.cadet-prep';
  var account = dir + '/afoqt_account.json';
  var gated = !!(window.CADET && window.CADET.url);

  function toLogin() {
    try { localStorage.removeItem('cadet_signed_in'); } catch (e) {}
    location.replace('login.html');
  }

  FS.mkdirTree(dir);
  FS.mount(IDBFS, { autoPersist: true }, dir);
  addRunDependency('progress');
  FS.syncfs(true, function (err) {
    if (err) console.warn('Could not load saved progress:', err);
    var handoff = null;
    try {
      handoff = localStorage.getItem('cadet_login');
      localStorage.removeItem('cadet_login');
    } catch (e) {}
    if (gated && handoff) FS.writeFile(account, handoff);
    if (gated && !FS.analyzePath(account).exists) { toLogin(); return; }  // the app never starts
    removeRunDependency('progress');
  });

  // Called by the app (CloudSync) when the sign-in is gone: Sign out, or it expired
  Module.cadetSignedOut = function () {
    if (gated) FS.syncfs(false, toLogin);
  };
});
