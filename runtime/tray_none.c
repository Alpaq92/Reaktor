#include "internal.h"
#include "reaktor/launch.h"

int  reaktor_tray_open(App *app, const reaktor_tray *spec)   { (void)app; (void)spec; return 0; }
void reaktor_tray_check(App *app, int item, int checked)     { (void)app; (void)item; (void)checked; }
void reaktor_tray_close(App *app)                            { (void)app; }
