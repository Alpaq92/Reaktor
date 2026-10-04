#include "keyboard_web.h"

#include <SDL3/SDL.h>
#include <emscripten.h>

static Uint32 g_type;

/* Through SDL's queue, so it stays in order with taps and keys. */
EMSCRIPTEN_KEEPALIVE void
reaktor_web_keys_typed(int code)
{
    SDL_Event e;

    SDL_zero(e);
    e.type = g_type;
    e.user.code = code;
    SDL_PushEvent(&e);
}


EM_JS(void, web_keys_make, (void), {
    var el = document.createElement("input");
    var canvas = Module["canvas"];
    /* Something for a soft Backspace to delete; never the app's text. */
    var base = " ";
    var k = {
        el: el, held: [], at: 0, composing: false, touching: 0, wanted: 0,
        coarse: window.matchMedia("(pointer: coarse)")
    };
    var soft = {
        "Enter": -2, "ArrowLeft": -3, "ArrowRight": -4, "ArrowUp": -5,
        "ArrowDown": -6, "Home": -7, "End": -8, "Delete": -9
    };
    var say = function (s) {
        if (window["reaktorKeylog"]) window["reaktorKeylog"]("keys: " + s);
    };
    var put = function (code) { Module["_reaktor_web_keys_typed"](code); };
    var reset = function () {
        el.value = base;
        el.setSelectionRange(base.length, base.length);
        k.held = [base];
        k.at = 0;
    };

    el.id = "reaktor-keys";
    el.type = "text";
    el.autocomplete = "off";
    el.setAttribute("autocorrect", "off");
    el.setAttribute("autocapitalize", "off");
    el.setAttribute("spellcheck", "false");
    el.setAttribute("aria-hidden", "true");
    el.tabIndex = -1;
    /* 16px, or Safari zooms the page in on the field it cannot see. */
    el.style.cssText = "position:fixed;left:0;top:0;width:1px;height:1px;" +
        "font-size:16px;opacity:0;border:0;padding:0;background:transparent;" +
        "color:transparent;pointer-events:none;";
    document.body.appendChild(el);
    reset();
    Module["reaktorKeys"] = k;

    k.take = function () {
        if (document.activeElement === el) return;
        el.focus({ preventScroll: true });
        reset();
        say("focus");
    };
    k.drop = function () {
        if (document.activeElement !== el) return;
        el.blur();
        say("blur");
    };

    var without = function (s, i) {
        return i < 0 ? s : s.slice(0, i).concat(s.slice(i + 1));
    };
    /* A composition is left alone until it ends, or the word is typed twice. */
    var sync = function () {
        var was = k.held, cur = Array.from(el.value), at = k.at;
        var p = 0, e = 0, back = 0, i, c;

        while (p < was.length && p < cur.length && was[p] === cur[p]) p++;
        while (e < was.length - p && e < cur.length - p &&
               was[was.length - 1 - e] === cur[cur.length - 1 - e]) e++;
        /* Deleting the base deletes the typed text too, so it is typed again. */
        if (at >= was.length - e) k.at = at + cur.length - was.length;
        else if (at >= p) { k.at = -1; back = 1; }
        was = without(was, at);
        cur = without(cur, k.at);
        p = 0;
        if (!back)
            while (p < was.length && p < cur.length && was[p] === cur[p]) p++;
        back += was.length - p;
        for (i = 0; i < back; i++) put(-1);
        for (i = p; i < cur.length; i++) {
            c = cur[i].codePointAt(0);
            if (c) put(c);
        }
        if (back || p < cur.length)
            say("queued " + back + " back, " + (cur.length - p) + " typed");
        k.held = Array.from(el.value);
        if (!k.composing) reset();
    };
    /* The caret stays at the end, or the next edit reads as a delete. */
    var pin = function () {
        var end = el.value.length;

        if (k.composing || document.activeElement !== el) return;
        if (el.selectionStart !== end || el.selectionEnd !== end)
            el.setSelectionRange(end, end);
    };

    el.addEventListener("compositionstart", function () { k.composing = true; });
    el.addEventListener("compositionend", function () {
        k.composing = false;
        sync();
    });
    el.addEventListener("input", function (e) {
        if (e["inputType"] !== "deleteCompositionText") sync();
    });
    document.addEventListener("selectionchange", pin);
    el.addEventListener("keydown", function (e) {
        var key = e["key"];

        /* A key the keyboard is composing with is its own, not SDL's. */
        if (e["isComposing"] || e["keyCode"] === 229) {
            e.stopPropagation();
            return;
        }
        if (/^(Home|End|PageUp|PageDown|Arrow(Left|Right|Up|Down))$/.test(key))
            e.preventDefault();
        /* A soft key has no code, so SDL cannot tell what it is. */
        if (!e["code"] && soft[key]) put(soft[key]);
    });
    el.addEventListener("blur", function () {
        k.composing = false;
        reset();
    });

    /* Canceling the touch keeps its mousedown from focusing the canvas. */
    canvas.addEventListener("pointerdown", function (e) {
        if (e.pointerType === "mouse" || !k.coarse.matches) return;
        e.preventDefault();
        k.touching++;
    });
    canvas.addEventListener("pointercancel", function (e) {
        if (e.pointerType !== "mouse" && k.touching > 0) k.touching--;
    });
    /* Safari opens the keyboard only for a focus inside the gesture. */
    canvas.addEventListener("pointerup", function (e) {
        var was;

        if (e.pointerType === "mouse" || !k.coarse.matches) return;
        if (k.touching > 0) k.touching--;
        if (!k.wanted && !onField(e.clientX, e.clientY)) { k.drop(); return; }
        if (k.composing) el.blur();
        was = document.activeElement === el;
        el.focus({ preventScroll: true });
        if (!was) reset();
        say(was ? "focus again" : "focus");
    });
    var onField = function (x, y) {
        var boxes = document.querySelectorAll("#a11y [role=textbox]" +
            ":not([aria-readonly=true]):not([aria-disabled=true])"), i, r;

        for (i = 0; i < boxes.length; i++) {
            r = boxes[i].getBoundingClientRect();
            if (x >= r.left - 4 && x <= r.right + 4 &&
                y >= r.top - 4 && y <= r.bottom + 4) return true;
        }
        return false;
    };
});

EM_JS(void, reaktor_web_keys_wants, (int wants), {
    var k = Module["reaktorKeys"];

    if (!k) return;
    k.wanted = wants;
    if (!k.coarse.matches || k.touching > 0) return;
    if (wants) k.take(); else k.drop();
});

void
reaktor_web_keys_init(unsigned event_type)
{
    g_type = event_type;
    web_keys_make();
}
