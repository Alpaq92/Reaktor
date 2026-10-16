#include "internal.h"
#include "reaktor/launch.h"

int  reaktor_inspector_open(App *app)  { (void)app; return 0; }
void reaktor_inspector_close(App *app) { (void)app; }
int  reaktor_inspector_shown(App *app) { (void)app; return 0; }

void reaktor_inspector_start(App *app, const reaktor_inspector_spec *spec, int flag)
{
    (void)app; (void)spec; (void)flag;
}
void reaktor_inspector_early(void)                             { }
void reaktor_inspector_stop(App *app)                          { (void)app; }
int  reaktor_inspector_event(App *app, const SDL_Event *e)     { (void)app; (void)e; return 0; }
void reaktor_inspector_frame(App *app)                         { app->why = 0; }
int  reaktor_inspector_marks(App *app)                         { (void)app; return 0; }
void reaktor_inspector_mark(App *app, struct nk_context *ctx, struct nk_command_buffer *o)
{
    (void)app; (void)ctx; (void)o;
}
const char *reaktor_inspector_sheet(void)                      { return NULL; }
