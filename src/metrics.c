/* metrics.c - see metrics.h.
 *
 * The scale lives in one place. Nothing else in the codebase should read a
 * DPI value directly; that is what keeps layout in logical units.
 *
 * The per-platform DPI queries this file used to carry (GetDpiForWindow,
 * SetProcessDpiAwarenessContext, and stubs for macOS and X11) are gone: SDL3
 * reports the scale portably and handles process awareness during SDL_Init. */
#include "metrics.h"

#include <stdlib.h>
#include <SDL3/SDL.h>

static float g_scale = 1.0f;

float curie_scale(void)
{
    return g_scale;
}

/* Honours a CURIE_SCALE override, in the spirit of GDK_SCALE / QT_SCALE_FACTOR.
 * Useful for testing HiDPI on a 96-DPI display, and a genuine escape hatch on
 * systems that report the wrong density. Returns 0 when unset or malformed. */
static float scale_from_env(void)
{
    const char *s = getenv("CURIE_SCALE");
    double v;
    char *end;

    if (!s || !*s) return 0.0f;
    v = strtod(s, &end);
    if (end == s || v <= 0.05 || v >= 100.0) return 0.0f;
    return (float)v;
}

void curie_set_scale(float scale)
{
    /* Guard against a zero or negative scale from a misbehaving display
     * query, which would collapse the whole UI to nothing. */
    if (scale > 0.05f && scale < 100.0f)
        g_scale = scale;
}

int curie_px(float logical)
{
    return (int)(logical * g_scale + 0.5f);
}

float curie_unpx(int device)
{
    return (float)device / g_scale;
}

void curie_px_rect(const float *logical_xywh, int *device_xywh)
{
    device_xywh[0] = curie_px(logical_xywh[0]);
    device_xywh[1] = curie_px(logical_xywh[1]);
    device_xywh[2] = curie_px(logical_xywh[2]);
    device_xywh[3] = curie_px(logical_xywh[3]);
}

void curie_dpi_enable_awareness(void)
{
    /* SDL_Init opts the process into per-monitor DPI awareness on the
     * platforms where that is a concept. Kept as a no-op so callers do not
     * need to know that. */
}

float curie_dpi_query_scale(void *native_window)
{
    float forced = scale_from_env();
    float scale;

    if (forced > 0.0f) return forced;
    if (!native_window) return 1.0f;

    scale = SDL_GetWindowDisplayScale((SDL_Window *)native_window);
    return scale > 0.0f ? scale : 1.0f;
}
