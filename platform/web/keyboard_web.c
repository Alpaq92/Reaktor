#include "keyboard_web.h"

#include <emscripten.h>

static void (*g_woke)(void);

/* No event of SDL's, and the runtime draws on events. */
EMSCRIPTEN_KEEPALIVE void
reaktor_web_keys_typed(void)
{
    if (g_woke) g_woke();
}

/* SDL's web backend turns `keypress` into text, which a phone never sends.
 * A focused input gets the text, and is what asks Android for a keyboard. */

EM_JS(void, web_keys_make, (void), {
    var el = document.createElement("input");

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
        "color:transparent;";
    document.body.appendChild(el);

    Module["reaktorKeys"] = { el: el, runes: [],
                              coarse: window.matchMedia("(pointer: coarse)") };

    /* Safari opens the keyboard only for a focus inside the gesture itself. */
    Module["canvas"].addEventListener("pointerup", function (e) {
        if (e.pointerType === "mouse") return;

        var boxes = document.querySelectorAll("#a11y [role=textbox]"), i;

        for (i = 0; i < boxes.length; i++) {
            var r = boxes[i].getBoundingClientRect();

            if (e.clientX >= r.left && e.clientX <= r.right &&
                e.clientY >= r.top && e.clientY <= r.bottom) {
                el.focus({ preventScroll: true });
                return;
            }
        }
        el.blur();
    });
    el.addEventListener("input", function () {
        var text = el.value, i;

        for (i = 0; i < text.length; i++) {
            var rune = text.codePointAt(i);

            Module["reaktorKeys"].runes.push(rune);
            if (rune > 0xFFFF) i++;
        }
        /* Empty, or a phone's backspace deletes here instead of in the app. */
        el.value = "";
        Module["_reaktor_web_keys_typed"]();
    });
});

/* Touch only: SDL prevents the browser's insertion, so a focused element
 * would swallow everything a physical keyboard types. */
EM_JS(void, reaktor_web_keys_wants, (int wants), {
    var k = Module["reaktorKeys"];

    if (!k) return;
    if (!k.coarse.matches) return;
    if (wants && document.activeElement !== k.el)
        k.el.focus({ preventScroll: true });
    else if (!wants && document.activeElement === k.el)
        k.el.blur();
});

EM_JS(int, reaktor_web_keys_holds, (void), {
    var k = Module["reaktorKeys"];

    return k && document.activeElement === k.el ? 1 : 0;
});

EM_JS(int, reaktor_web_keys_take, (void), {
    var k = Module["reaktorKeys"];

    return k && k.runes.length ? k.runes.shift() : 0;
});

void
reaktor_web_keys_init(void (*woke)(void))
{
    g_woke = woke;
    web_keys_make();
}
