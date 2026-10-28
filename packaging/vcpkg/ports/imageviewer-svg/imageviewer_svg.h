/* ImageViewer's SVG renderer contract (decision D-90).
 *
 * The application draws SVG only through these five functions. Any renderer can stand behind
 * them: today resvg, through lib.rs beside this file. Replacing it means another implementation
 * of this header and its port (packaging/vcpkg/ports/), and nothing in the application.
 *
 * What every implementation must guarantee:
 * - nothing outside the document is read: no file, no network; an embedded image is a data: URL
 *   (PNG, JPEG, GIF, WebP or SVG) or nothing. The only files read are the system's fonts, and only
 *   when the caller asks for them;
 * - no function aborts the process or lets an error escape: each returns a code;
 * - the functions may be called from one thread at a time (the decode worker has one).
 *
 * The caller (src/svg.cpp, in the decode worker, D-38) gives the document already inflated (never
 * gzip) and made safe (safeSvg(), D-71), and bounds the pixels it asks for. */
#ifndef IMAGEVIEWER_SVG_H
#define IMAGEVIEWER_SVG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Raised when a function's meaning changes; a renderer built for another version is not used. */
#define IV_SVG_CONTRACT_VERSION 1

enum iv_svg_status {
    IV_SVG_OK = 0,
    IV_SVG_BAD_ARGUMENT = 1, /* a null pointer, a size of 0, a buffer the renderer cannot address */
    IV_SVG_UNREADABLE = 2,   /* not an SVG document, or one the renderer refuses (too many elements) */
    IV_SVG_FAILED = 3        /* the renderer failed while working (a panic, caught) */
};

typedef struct iv_svg iv_svg; /* a parsed document */

/* The contract version this renderer implements: IV_SVG_CONTRACT_VERSION. */
int iv_svg_contract_version(void);

/* The renderer's name and version, for Settings > Image Formats, e.g. "resvg 0.48.1". */
const char *iv_svg_renderer(void);

/* Parses `size` bytes of SVG (UTF-8 XML). With `load_fonts` non-zero, the system's fonts are
 * loaded first, for the document's text; without, text is not drawn. On IV_SVG_OK, *out is the
 * document, to free with iv_svg_free(). */
int iv_svg_parse(const uint8_t *data, size_t size, int load_fonts, iv_svg **out);

/* The document's own size in CSS pixels (its width and height, or its viewBox's): both > 0. */
int iv_svg_size(const iv_svg *svg, double *width, double *height);

/* Draws the document into `pixels`, `width` x `height` RGBA, 8 bits per channel, premultiplied
 * alpha, rows from the top, `width` * 4 bytes each (sRGB values, as SVG's colours are): the
 * document's point (x, y) lands at (x * scale - left, y * scale - top). Every byte of `pixels` is
 * written: what the document does not cover is transparent. */
int iv_svg_render(const iv_svg *svg, double scale, double left, double top, uint32_t width, uint32_t height,
                  uint8_t *pixels);

/* Frees a document from iv_svg_parse(); null does nothing. */
void iv_svg_free(iv_svg *svg);

#ifdef __cplusplus
}
#endif

#endif
