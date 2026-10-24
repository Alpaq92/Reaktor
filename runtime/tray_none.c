#include "internal.h"
#include "reaktor/launch.h"

int  reaktor_tray_open(App *app, const reaktor_tray *spec)   { (void)app; (void)spec; return 0; }
void reaktor_tray_check(App *app, int item, int checked)     { (void)app; (void)item; (void)checked; }
void reaktor_tray_close(App *app)                            { (void)app; }
void reaktor_tray_hide_on_minimize(App *app, int on)         { (void)app; (void)on; }
void reaktor_tray_event(App *app, const SDL_Event *e)        { (void)app; (void)e; }
