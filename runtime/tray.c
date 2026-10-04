#include "internal.h"
#include "reaktor/launch.h"

#define TRAY_ITEMS 16

static SDL_Tray          *g_tray;
static SDL_TrayEntry     *g_entry[TRAY_ITEMS];
static reaktor_tray_item  g_item[TRAY_ITEMS];
static int                g_n;

static void SDLCALL
chosen(void *userdata, SDL_TrayEntry *entry)
{
    const reaktor_tray_item *it = &g_item[(intptr_t)userdata];
    App *app = reaktor_main_app();

    if (!app || !it->chosen) return;
    it->chosen(app, it->checkbox && SDL_GetTrayEntryChecked(entry), it->user);
    reaktor_wake(app);
}

int
reaktor_tray_open(App *app, const reaktor_tray *spec)
{
#ifdef __EMSCRIPTEN__
    (void)app; (void)spec;
    return 0;
#else
    SDL_TrayMenu *menu;
    SDL_Surface  *icon;
    int i;

    reaktor_tray_close(app);
    if (!spec) return 0;
    icon   = reaktor_icon_surface(reaktor_launch_icon(), 32);
    {
        /* GTK, under the tray on Linux and the BSDs, sets the user's locale. */
        char *was = reaktor_c_locale_keep(0);

        g_tray = SDL_CreateTray(icon, spec->tooltip);
        reaktor_c_locale_put(0, was);
    }
    if (icon) SDL_DestroySurface(icon);
    if (!g_tray) {
        SDL_Log("tray: %s", SDL_GetError());
        return 0;
    }
    menu = SDL_CreateTrayMenu(g_tray);
    g_n  = spec->count < 0 ? 0 : spec->count < TRAY_ITEMS ? spec->count : TRAY_ITEMS;
    for (i = 0; i < g_n; i++) {
        const reaktor_tray_item *it = &spec->items[i];
        SDL_TrayEntryFlags f = it->checkbox ? SDL_TRAYENTRY_CHECKBOX : SDL_TRAYENTRY_BUTTON;

        if (it->checkbox && it->checked) f |= SDL_TRAYENTRY_CHECKED;
        if (it->disabled) f |= SDL_TRAYENTRY_DISABLED;
        g_item[i]  = *it;
        g_entry[i] = menu ? SDL_InsertTrayEntryAt(menu, -1, it->label, f) : NULL;
        if (g_entry[i] && it->label)
            SDL_SetTrayEntryCallback(g_entry[i], chosen, (void *)(intptr_t)i);
    }
    return 1;
#endif
}

void
reaktor_tray_check(App *app, int item, int checked)
{
    (void)app;
    if (item < 0 || item >= g_n || !g_entry[item] || !g_item[item].checkbox) return;
    if (SDL_GetTrayEntryChecked(g_entry[item]) != (checked != 0))
        SDL_SetTrayEntryChecked(g_entry[item], checked != 0);
}

void
reaktor_tray_close(App *app)
{
    (void)app;
    if (g_tray) SDL_DestroyTray(g_tray);
    g_tray = NULL;
    g_n    = 0;
}
