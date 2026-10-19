/* PDFGen as it ships, minus one clash: NetBSD's <sys/types.h> declares a
   bswap32 and wraps it in a macro, and PDFGen defines a static function of
   that name. PDFGen's is renamed; the header's guard keeps it from coming back. */
#include <sys/types.h>
#undef bswap32
#define bswap32 pdfgen_bswap32

#include "pdfgen.c"
