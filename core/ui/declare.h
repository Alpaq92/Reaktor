#ifndef REAKTOR_DECLARE_H
#define REAKTOR_DECLARE_H

#include "layout.h"
#include "reaktor/widgets.h"

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

void reaktor_frame_begin(App *app, struct nk_context *ctx, struct nk_rect area);
void reaktor_frame_end(void);

int reaktor_frame_settled(const App *app);

#endif
