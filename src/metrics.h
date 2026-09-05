/* metrics.h - HiDPI scaling, modelled on LCUI's ui/metrics.h.
 *
 * The rule: **lay out in logical (CSS) pixels; apply scale only at the paint
 * and input boundary.** Layout code never sees the scale factor. Everything
 * that crosses into device pixels goes through curie_px().
 *
 * Only curie_dpi_enable_awareness() and curie_dpi_query_scale() are
 * platform-specific; both become one SDL3 call once the backend lands
 * (SDL_GetWindowDisplayScale), so the rest of this module never changes. */
#ifndef CURIE_METRICS_H
#define CURIE_METRICS_H

/* Current device-pixel ratio. 1.0 = 96 DPI, 1.5 = 144 DPI, 2.0 = 192 DPI. */
float curie_scale(void);
void  curie_set_scale(float scale);

/* Logical pixels -> device pixels. Rounds rather than truncates: truncation
 * accumulates a half-pixel of drift across nested boxes. */
int   curie_px(float logical);

/* Device pixels -> logical. Used on input coordinates, which arrive scaled. */
float curie_unpx(int device);

/* Scales a logical rect into device space in one call. */
void  curie_px_rect(const float *logical_xywh, int *device_xywh);

/* --- platform seam (the only part that changes per backend) --- */

/* Opt the process into per-monitor DPI awareness. Call once, before any
 * window exists. No-op where the concept does not apply. */
void  curie_dpi_enable_awareness(void);

/* Reads the scale for the display hosting `native_window` (HWND today,
 * SDL_Window* later). Returns 1.0f if it cannot be determined. */
float curie_dpi_query_scale(void *native_window);

#endif /* CURIE_METRICS_H */
