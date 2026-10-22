var statusEl = document.getElementById('status');
var barEl = statusEl.querySelector('.bar');
var textEl = statusEl.querySelector('.text');
var Module = {
  canvas: document.getElementById('canvas'),
  print: function (text) { console.log(text); },
  printErr: function (text) { console.error(text); },
  setStatus: function (text) {
    if (!statusEl) return;
    if (text) {
      var m = /\((\d+)\/(\d+)\)/.exec(text);
      if (m && +m[2] > 0) {
        barEl.classList.add('known');
        barEl.firstChild.style.width = Math.min(100, 100 * m[1] / m[2]) + '%';
      }
      textEl.textContent = text.replace('...', '…');
      return;
    }
    var el = statusEl;
    statusEl = null;
    el.classList.add('done');
    setTimeout(function () { el.remove(); }, 260);
  }
};

Module.canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });
Module.canvas.addEventListener('keydown', function (e) {
  if (e.key === 'Tab') e.preventDefault();
});

if (new URLSearchParams(location.search).has('keylog')) {
  var keylog = document.createElement('script');
  keylog.src = 'keylog.js';
  document.head.appendChild(keylog);
}

Module.onRuntimeInitialized = function () { Module.setStatus(''); };
