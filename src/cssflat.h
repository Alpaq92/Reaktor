/* cssflat.h - narrows CSS down to what LCUI's engine actually implements.
 *
 * LCUI parses type, #id, .class and :status selectors joined by descendant
 * combinators, and 85 properties. It does not implement custom properties -
 * css_computed_style_t declares a custom_props field, but nothing in lib/css
 * ever reads it - and it has no @media at all.
 *
 * That matters here because the palette *is* custom properties: tiny.css keeps
 * every colour in :root, in variables-light.css and variables-dark.css, and
 * writes its rules in terms of them. Substituting here, before the engine sees
 * the text, is what lets the sheets be read as they ship - no value is copied
 * out of the submodule and into this tree.
 *
 * What this pass does:
 *   - strips comments;
 *   - collects --name declarations from :root and from the [data-theme="..."]
 *     block for the active theme, resolving var() chains among them;
 *   - substitutes var(--name[, fallback]) everywhere else, dropping any
 *     declaration whose value cannot be resolved;
 *   - drops @media blocks, and selectors carrying [attr] or ::pseudo, which
 *     LCUI's parser cannot represent.
 *
 * The resolved map is handed back too. Some of the palette never reaches a
 * rule this engine can apply - the surfaces the application paints itself, and
 * anything behind a selector dropped above - so those tokens are read straight
 * from the map instead. */
#ifndef REAKTOR_CSSFLAT_H
#define REAKTOR_CSSFLAT_H

#include <stddef.h>

typedef struct reaktor_cssvars reaktor_cssvars;

/* Reads and flattens `count` stylesheets in order. `theme` selects which
 * [data-theme="..."] block contributes its custom properties ("light" or
 * "dark"). Returns malloc'd CSS text the caller frees, or NULL on failure.
 * When `out_vars` is non-NULL it receives the resolved custom properties. */
char *reaktor_css_flatten(const char *const *paths, int count,
                        const char *theme, reaktor_cssvars **out_vars);

/* Resolved value of a custom property, e.g. "--background-body", or NULL. */
const char *reaktor_cssvars_get(const reaktor_cssvars *vars, const char *name);

/* Resolved custom property parsed as #rgb/#rrggbb. Returns 0 if absent or
 * not a hex colour. */
int reaktor_cssvars_color(const reaktor_cssvars *vars, const char *name,
                        unsigned char rgba[4]);

void reaktor_cssvars_free(reaktor_cssvars *vars);

#endif /* REAKTOR_CSSFLAT_H */
