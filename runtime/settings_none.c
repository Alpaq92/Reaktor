#include "internal.h"

const char *reaktor_setting(const char *key)                     { (void)key; return NULL; }
void        reaktor_setting_set(const char *key, const char *v)  { (void)key; (void)v; }
void        reaktor_settings_save(void)                          {}
void        reaktor_settings_clear(void)                         {}
void        reaktor_settings_free(void)                          {}
void        reaktor_window_restore(App *app)                     { (void)app; }
void        reaktor_window_remember(App *app)                    { (void)app; }
