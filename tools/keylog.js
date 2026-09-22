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
  /* Stamped as it happens, drawn once a frame, or the log times itself. */
  var say = function (s) {
    text += ((Date.now() - t0) / 1000).toFixed(2) + ' ' + s + '\n';
    if (!pending) { pending = 1; requestAnimationFrame(flush); }
  };
  var saw = function (e) {
    say(e.type + ' ' + who(e.target) +
        ' active=' + who(document.activeElement) +
        (e.key === undefined ? '' : ' key=' + e.key + ' keyCode=' + e.keyCode) +
        (e.inputType === undefined ? '' : ' ' + (e.inputType || '-')) +
        (e.data === undefined ? '' : ' data=' + JSON.stringify(e.data)) +
        (e.isComposing ? ' composing' : ''));
  };

  ('focusin focusout pointerdown pointerup touchstart touchend mousedown ' +
   'mouseup click keydown keyup beforeinput input compositionstart ' +
   'compositionupdate compositionend').split(' ').forEach(function (t) {
    addEventListener(t, saw, true);
  });

  if (window.visualViewport) {
    visualViewport.addEventListener('resize', function () {
      say('viewport ' + Math.round(visualViewport.height));
    });
  }
}());
