#ifndef REAKTOR_TEXT_H
#define REAKTOR_TEXT_H

#include "reaktor/text.h"

struct nk_context;
struct nk_font;
struct SDL_Renderer;

void reaktor_text_add_fallback(const char *path);
void reaktor_text_attach_font(struct nk_font *font, const char *path);
void reaktor_text_prepare(struct nk_context *ctx, struct SDL_Renderer *renderer);

#endif
