/* metrics.h - HiDPI scaling, modelled on LCUI's ui/metrics.h.
 *
 * The rule: **lay out in logical (CSS) pixels; apply scale only at the paint
 * and input boundary.** Layout code never sees the scale factor. Everything
 * that crosses into device pixels goes through reaktor_px().
 *
 * Nothing here is platform-specific any more. The two per-backend DPI calls
 * this module was built around became SDL_GetWindowDisplayScale and a part of
 * SDL_Init; both are kept as functions so callers need not know that. */
#ifndef REAKTOR_METRICS_H
#define REAKTOR_METRICS_H

/* Current device-pixel ratio. 1.0 = 96 DPI, 1.5 = 144 DPI, 2.0 = 192 DPI. */
float reaktor_scale(void);
void  reaktor_set_scale(float scale);

/* Logical pixels -> device pixels. Rounds rather than truncates: truncation
 * accumulates a half-pixel of drift across nested boxes. */
int   reaktor_px(float logical);

/* --- the display scale, which SDL answers everywhere --- */

/* Opt the process into per-monitor DPI awareness. Call once, before any
 * window exists. No-op where the concept does not apply. */
void  reaktor_dpi_enable_awareness(void);

/* Reads the scale for the display hosting `native_window`, an SDL_Window *.
 * Returns 1.0f if it cannot be determined. */
float reaktor_dpi_query_scale(void *native_window);

#endif /* REAKTOR_METRICS_H */
