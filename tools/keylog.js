/* ?keylog: what the browser and its keyboard actually send. */
(function () {
  var log = document.createElement('pre');

  log.style.cssText = 'position:fixed;left:0;right:0;bottom:0;margin:0;' +
    'max-height:46%;overflow:hidden;z-index:9;padding:4px;' +
    'pointer-events:none;background:rgba(0,0,0,.84);color:#9f9;' +
    'white-space:pre-wrap;font:11px/1.35 ui-monospace,Menlo,monospace;';
  document.body.appendChild(log);

  var t0 = Date.now(), text = '', pending = 0;

  var who = function (n) {
    return !n ? 'none' : (n.id || n.tagName || '?').toLowerCase();
  };
  var flush = function () {
    pending = 0;
    log.textContent = text;
    log.scrollTop = log.scrollHeight;
  };
  /* Stamped as it happens and written in one route, so lines stay in order. */
  var add = function (line) {
    text += line + '\n';
    if (!pending) { pending = 1; requestAnimationFrame(flush); }
  };
  var say = function (s) {
    var at = ((Date.now() - t0) / 1000).toFixed(2);

    setTimeout(function () { add(at + ' ' + s); }, 0);
  };
  var saw = function (e) {
    var at = ((Date.now() - t0) / 1000).toFixed(2);
    var line = e.type + ' ' + who(e.target) +
        ' active=' + who(document.activeElement) +
        (e.pointerType ? ' ' + e.pointerType : '') +
        (e.key === undefined ? '' : ' key=' + e.key + ' code=' +
            JSON.stringify(e.code) + ' keyCode=' + e.keyCode) +
        (e.inputType === undefined ? '' : ' ' + (e.inputType || '-')) +
        (e.data === undefined ? '' : ' data=' + JSON.stringify(e.data)) +
        (e.isComposing ? ' composing' : '');

    /* Read once every listener has run, SDL's among them. */
    setTimeout(function () {
      add(at + ' ' + line + (e.defaultPrevented ? ' PREVENTED' : ''));
    }, 0);
  };

  ('focusin focusout pointerdown pointerup pointercancel touchstart touchend ' +
   'mousedown mouseup click keydown keypress keyup beforeinput input ' +
   'compositionstart compositionupdate compositionend').split(' ')
    .forEach(function (t) { addEventListener(t, saw, true); });
  addEventListener('blur', function () { say('window blur'); });
  addEventListener('focus', function () { say('window focus'); });

  window.reaktorKeylog = say;

  if (window.visualViewport) {
    visualViewport.addEventListener('resize', function () {
      say('viewport ' + Math.round(visualViewport.height));
    });
  }
}());
