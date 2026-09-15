#ifndef REAKTOR_TEXT_H
#define REAKTOR_TEXT_H

struct nk_context;
struct nk_font;
struct SDL_Renderer;

void reaktor_text_add_fallback(const char *path);
void reaktor_text_attach_font(struct nk_font *font, const char *path);
void reaktor_text_prepare(struct nk_context *ctx, struct SDL_Renderer *renderer);

/* Where the next line starts, at or before limit; 0 if nowhere. */
int  reaktor_text_break(const char *s, int len, int limit);
int  reaktor_text_rtl(const char *s, int len);
void reaktor_text_describe(char *buf, int cap);

#endif
