#ifndef REAKTOR_FTOPTION_H
#define REAKTOR_FTOPTION_H

#include <freetype/config/ftoption.h>

/* FreeType's options as shipped, less what Reaktor never reads: an
   environment override, compressed and Mac fonts, SVG and color glyphs,
   bitmap strikes, variations and the TrueType bytecode hinter. */
#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES
#undef FT_CONFIG_OPTION_USE_LZW
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_MAC_FONTS
#undef FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK
#undef FT_CONFIG_OPTION_INCREMENTAL
#undef FT_CONFIG_OPTION_SVG
#undef TT_CONFIG_OPTION_EMBEDDED_BITMAPS
#undef TT_CONFIG_OPTION_COLOR_LAYERS
#undef TT_SUPPORT_COLRV1
#undef TT_CONFIG_OPTION_BDF
#undef TT_CONFIG_OPTION_GX_VAR_SUPPORT
#undef TT_CONFIG_OPTION_VARC
#undef TT_CONFIG_OPTION_BYTECODE_INTERPRETER
#undef TT_CONFIG_OPTION_SUBPIXEL_HINTING
#undef TT_USE_BYTECODE_INTERPRETER
#undef TT_SUPPORT_SUBPIXEL_HINTING_MINIMAL

#endif
