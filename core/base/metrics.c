#include "metrics.h"

#include <stdlib.h>
#include <SDL3/SDL.h>

static float g_scale = 1.0f;

float reaktor_scale(void)
{
    return g_scale;
}

void reaktor_set_scale(float scale)
{
    if (scale > 0.05f && scale < 100.0f)
        g_scale = scale;
}

int reaktor_px(float logical)
{
    return (int)(logical * g_scale + 0.5f);
}

float reaktor_dpi_query_scale(void *native_window)
{
    float scale;

    if (!native_window) return 1.0f;
    scale = SDL_GetWindowDisplayScale((SDL_Window *)native_window);
    return scale > 0.0f ? scale : 1.0f;
}
