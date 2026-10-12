Module['preRun'] = [].concat(Module['preRun'] || [], function () {
    FS.mkdir('/libsdl');
    FS.mount(IDBFS, {}, '/libsdl');
    addRunDependency('reaktor-settings');
    FS.syncfs(true, function (err) {
        if (err) console.warn('settings: ' + err);
        removeRunDependency('reaktor-settings');
    });
});
