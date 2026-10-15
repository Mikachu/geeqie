/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "main.h"

#include "image-load.h"
#include "image_load_psd.h"
#include "filedata.h"

#include <string.h>

typedef struct ImageLoaderPsd ImageLoaderPsd;
struct ImageLoaderPsd {
    ImageLoaderBackendCbAreaUpdated area_updated_cb;
    ImageLoaderBackendCbSize size_cb;

    gpointer data;

    GdkPixbuf *pixbuf;
    guint requested_width;
    guint requested_height;

    gboolean abort;
    gboolean is_psb;
};

static gpointer image_loader_psd_new(ImageLoaderBackendCbAreaUpdated area_updated_cb,
                                     ImageLoaderBackendCbSize size_cb, gpointer data)
{
    ImageLoaderPsd *loader = g_new0(ImageLoaderPsd, 1);

    loader->area_updated_cb = area_updated_cb;
    loader->size_cb         = size_cb;
    loader->data            = data;

    return (gpointer)loader;
}

static void image_loader_psd_set_size(gpointer loader, int width, int height)
{
    ImageLoaderPsd *lp = (ImageLoaderPsd *)loader;

    lp->requested_width  = width;
    lp->requested_height = height;
}

/* ---- big/little-endian helpers, PSD is big-endian ---- */

static guint64 psd_read_u64(const guchar *p) { guint64 v; memcpy(&v, p, 8); return GUINT64_FROM_BE(v); }
static guint32 psd_read_u32(const guchar *p) { guint32 v; memcpy(&v, p, 4); return GUINT32_FROM_BE(v); }
static guint16 psd_read_u16(const guchar *p) { guint16 v; memcpy(&v, p, 2); return GUINT16_FROM_BE(v); }

/* PSD header fields needed by both the embedded-thumbnail path and the
 * composite-image fallback. */
typedef struct PsdHeader PsdHeader;
struct PsdHeader {
    guint16 version; /* 1 for PSD, 2 for PSB */
    guint16 channels;
    guint32 height;
    guint32 width;
    guint16 depth;
    guint16 color_mode;
};

/* Attempt to locate and decode resource 1036 (raw RGB thumbnail) or
 * resource 1033 (JPEG thumbnail) from the Image Resources section.
 * Ported from the "8BIM"/1033/1036 walk in the reference PSDParser::ExtractThumbnail,
 * with IStream::Read/Seek replaced by direct pointer/offset arithmetic
 * into the in-memory buffer.
 * On return, *resources_end_out is set to the absolute offset just past
 * the Image Resources section (needed by the caller to locate the Layer
 * and Mask Information section for the composite-image fallback). */
static GdkPixbuf *psd_extract_embedded_thumbnail(const guchar *data, gsize len,
                                                 ImageLoaderPsd *lp,
                                                 gsize *resources_end_out)
{
    /* psd_read_header already validated this stuff */
    gsize pos = 26;

    if (pos + 4 > len) return NULL;
    guint32 color_mode_len = psd_read_u32(data + pos); pos += 4;
    pos += color_mode_len;

    if (pos + 4 > len) return NULL;
    guint32 image_resources_len = psd_read_u32(data + pos); pos += 4;
    gsize resources_start = pos;
    gsize resources_end   = resources_start + image_resources_len;
    if (resources_end > len) return NULL;
    *resources_end_out = resources_end;

    while (pos + 4 <= resources_end && !g_atomic_int_get(&lp->abort))
    {
        if (memcmp(data + pos, "8BIM", 4) != 0) break;
        pos += 4;

        if (pos + 3 > resources_end) break;
        guint16 resource_id = psd_read_u16(data + pos); pos += 2;

        /* this doesn't read so no need for bounds check */
        guint8 name_len = data[pos]; pos += 1;
        pos += name_len;
        if ((name_len + 1) % 2 != 0) pos += 1; /* pad to even */

        if (pos + 4 > resources_end) break;
        guint32 resource_size = psd_read_u32(data + pos); pos += 4;
        gsize resource_start = pos;

        /* resource payload itself must not run past the resources section
         * (and therefore not past len, since resources_end <= len) */
        if (resource_start + resource_size > resources_end) break;

        if ((resource_id == 1036 || resource_id == 1033) && resource_size > 28)
        {
            guint32 format  = psd_read_u32(data + pos);
            guint32 thumb_w = psd_read_u32(data + pos + 4);
            guint32 thumb_h = psd_read_u32(data + pos + 8);
            guint32 width_b = psd_read_u32(data + pos + 12);
            /* totalSize, sizeAfterCompression, bitsPerPixel, planes: skipped */
            gsize payload_offset = pos + 28;
            guint32 payload_size = resource_size - 28;

            if (payload_offset + payload_size > len ||
                thumb_w == 0 || thumb_h == 0 || thumb_w > 30000 || thumb_h > 30000)
            {
                /* malformed/truncated resource, skip */
            }
            else if (format == 1)
            {
                /* format == 1: JPEG-compressed payload (the common case in real
                 * files). Feed the payload -- NOT including the 28-byte header --
                 * to gdk-pixbuf's own jpeg backend. */
                GdkPixbufLoader *jl = gdk_pixbuf_loader_new();
                GdkPixbuf *pb = NULL;
                if (gdk_pixbuf_loader_write(jl, data + payload_offset, payload_size, NULL) &&
                    gdk_pixbuf_loader_close(jl, NULL))
                {
                    pb = gdk_pixbuf_loader_get_pixbuf(jl);
                    if (pb) g_object_ref(pb);
                }
                g_object_unref(jl);
                if (pb) return pb;
            }
            else if (format == 0)
            {
                /* format == 0: raw interleaved RGB payload. */
                guint64 raw_size = (guint64)width_b * thumb_h;
                if (width_b >= thumb_w * 3 && (gsize)payload_size >= raw_size)
                {
                    guchar *rgb = g_memdup2(data + payload_offset, raw_size);
                    return gdk_pixbuf_new_from_data(rgb, GDK_COLORSPACE_RGB, FALSE, 8,
                                                    thumb_w, thumb_h, (gsize)width_b,
                                                    (GdkPixbufDestroyNotify)g_free, NULL);
                }
            }
        }
        pos = resource_start + resource_size;
        if (resource_size % 2 != 0) pos += 1;
    }

    return NULL;
}

/* Decode PackBits/RLE-compressed scanlines for one channel plane,
 * writing decoded bytes directly into dest at the given stride (e.g.
 * stride 4 to write straight into an interleaved RGBA buffer, offset
 * by channel index -- avoids a separate planar buffer + conversion
 * pass entirely). Pass stride 0 with a 1-byte scratch dest to discard
 * a channel's data while still advancing *pos_io correctly. */
static inline gboolean psd_decode_rle_channel(const guchar *data, gsize len,
                                              ImageLoaderPsd *lp,
                                              gsize *pos_io, const guint32 *byte_counts,
                                              guint32 height, guint32 width, guint32 bytes_per_sample,
                                              guchar *dest, guint32 dest_stride)
{
    gsize pos = *pos_io;

    for (guint32 line = 0; line < height; line++)
    {
        gsize dest_offset = (gsize)line * width * dest_stride;
        guint32 line_bytes = byte_counts[line];
        if (pos + line_bytes > len) return FALSE;

        const guchar *src = data + pos;
        guint32 src_pos = 0;
        guint32 raw_bytes_needed = width * bytes_per_sample;
        guint32 raw_bytes_written = 0;
        guint32 byte_in_sample = 0; /* 0 == MSB, gets written; others skipped */

        while (src_pos < line_bytes && raw_bytes_written < raw_bytes_needed)
        {
            if (g_atomic_int_get(&lp->abort)) return FALSE;

            gint8 n = (gint8) src[src_pos++];

            if (n >= 0)
            {
                guint32 count = (guint32) n + 1;
                for (guint32 j = 0;
                     j < count && src_pos < line_bytes &&
                     raw_bytes_written < raw_bytes_needed;
                     j++)
                {
                    if (byte_in_sample == 0)
                    {
                        dest[dest_offset] = src[src_pos++];
                        dest_offset += dest_stride;
                    }
                    else
                    {
                        src_pos++;
                    }
                    byte_in_sample = !byte_in_sample % bytes_per_sample;
                    raw_bytes_written++;
                }
            }
            else if (n != -128)
            {
                guint32 count = (guint32)(-n) + 1;
                if (src_pos < line_bytes)
                {
                    guchar value = src[src_pos++];
                    for (guint32 j = 0; j < count && raw_bytes_written < raw_bytes_needed; j++)
                    {
                        if (byte_in_sample == 0)
                        {
                            dest[dest_offset] = value;
                            dest_offset += dest_stride;
                        }
                        byte_in_sample = !byte_in_sample % bytes_per_sample;
                        raw_bytes_written++;
                    }
                }
            }
            /* n == -128 is a no-op */
        }
        pos += line_bytes;
    }
    *pos_io = pos;
    return TRUE;
}

static gboolean psd_decode_rle_channel_8(const guchar *data, gsize len,
                                         ImageLoaderPsd *lp,
                                         gsize *pos_io, const guint32 *byte_counts,
                                         guint32 height, guint32 width,
                                         guchar *dest, guint32 dest_stride)
{
    return psd_decode_rle_channel(data, len, lp, pos_io, byte_counts,
                                  height, width, 1, dest, dest_stride);
}

static gboolean psd_decode_rle_channel_16(const guchar *data, gsize len,
                                          ImageLoaderPsd *lp,
                                          gsize *pos_io, const guint32 *byte_counts,
                                          guint32 height, guint32 width,
                                          guchar *dest, guint32 dest_stride)
{
    return psd_decode_rle_channel(data, len, lp, pos_io, byte_counts,
                                  height, width, 2, dest, dest_stride);
}

/* TRUE if the first extra channel of the merged image is transparency:
 * the layer count (first int16 of the Layer Info block) is negative. */
static gboolean psd_merged_has_transparency(const guchar *data, gsize len,
                                            gsize pos, const PsdHeader *h)
{
    const gsize lf = h->version == 1 ? 4 : 8;   /* length field size */

    if (len < pos || len - pos < lf) return FALSE;
    guint64 section = lf == 4 ? psd_read_u32(data + pos) : psd_read_u64(data + pos);
    pos += lf;
    if (section == 0 || len - pos < lf + 2) return FALSE;

    guint64 info = lf == 4 ? psd_read_u32(data + pos) : psd_read_u64(data + pos);
    pos += lf;
    if (info < 2) return FALSE;

    return (gint16) psd_read_u16(data + pos) < 0;
}

/* Fallback used when no embedded thumbnail resource (1033/1036) is present:
 * decode the full composite image data (skipping the Layer and Mask
 * Information section) and downscale it ourselves. Ported from the second
 * half of the reference PSDParser::ExtractThumbnail(). Only 8/16-bit-per-
 * channel RGB/RGBA (color_mode == 3) and uncompressed/PackBits compression
 * are supported, matching the reference implementation's own limitations. */
static GdkPixbuf *psd_decode_composite(const guchar *data, gsize len,
                                       ImageLoaderPsd *lp,
                                       gsize pos, const PsdHeader *header)
{
    guint32 width  = header->width;
    guint32 height = header->height;

    if (g_atomic_int_get(&lp->abort)) return NULL;

    if (header->depth != 8 && header->depth != 16) return NULL;
    if (header->color_mode != 3)   return NULL; /* RGB only */
    if (header->channels < 3)      return NULL; /* only handle actual RGB/RGBA for now */
    if (header->channels > 56)     return NULL; /* max according to spec */
    if (width == 0 || height == 0) return NULL;
    guint32 limit = header->version == 1 ? 30000 : 300000;
    if (width > limit || height > limit) return NULL;

    /* Must look at the Layer and Mask section header, so before it's skipped.
     * pos is still at its length field here. */
    const gboolean has_alpha = header->channels >= 4 &&
                               psd_merged_has_transparency(data, len, pos, header);

    /* Skip Layer and Mask Information section */
    if (header->version == 1)
    {
        if (pos + 4 > len) return NULL;
        guint32 layer_mask_len = psd_read_u32(data + pos); pos += 4;
        pos += layer_mask_len;
    }
    else // PSB
    {
        if (pos + 8 > len) return NULL;
        guint64 layer_mask_len = psd_read_u64(data + pos); pos += 8;
        pos += layer_mask_len;
    }
    if (pos > len) return NULL;

    /* Image Data section */
    if (pos + 2 > len) return NULL;
    guint16 compression = psd_read_u16(data + pos); pos += 2;
    if (compression > 1) return NULL; /* only raw or RLE supported */

    guint32 bytes_per_sample = header->depth / 8;
    guint64 channel_size     = (gsize) width * height * bytes_per_sample;
    guint32 channels         = header->channels;
    guint32 stride           = has_alpha ? 4 : 3;

    if ((guint64) width * height * stride > 1024ULL * 1024 * 1024) return NULL;

    guchar *rgba = g_malloc((gsize) width * height * stride);
    memset(rgba, 255,       (gsize) width * height * stride);

    if (compression == 0)
    {
        gsize total = (gsize) channel_size * channels;
        if (pos + total > len) { g_free(rgba); return NULL; }
        const guchar *plane_src = data + pos;
        pos += total;

        for (guint32 i = 0; i < channel_size; i += bytes_per_sample)
        {
            if (g_atomic_int_get(&lp->abort)) { g_free(rgba); return NULL; }
            guint32 px = i / bytes_per_sample;
            rgba[px * stride + 0] = plane_src[                   i];
            rgba[px * stride + 1] = plane_src[channel_size     + i];
            rgba[px * stride + 2] = plane_src[channel_size * 2 + i];
            if (has_alpha) /* unindent for alignment */
            rgba[px * stride + 3] = plane_src[channel_size * 3 + i];
        }
    }
    else /* compression == 1, PackBits RLE */
    {
        gsize scanline_count = height * channels;
        if (pos + scanline_count * 2 * header->version > len) { g_free(rgba); return NULL; }

        guint32 *byte_counts = g_new(guint32, scanline_count);
        for (gsize i = 0; i < scanline_count; i++)
        {
            if (header->version == 1) { byte_counts[i] = psd_read_u16(data + pos); pos += 2; }
            else                      { byte_counts[i] = psd_read_u32(data + pos); pos += 4; }
        }

        gboolean ok = TRUE;
        for (gsize c = 0; c < stride && ok; c++)
        {
            /* channels 0..3 map directly to R,G,B,A in the interleaved
             * buffer; anything beyond that (spot channels) is ignored */
            if (bytes_per_sample == 1)
                ok = psd_decode_rle_channel_8(data, len, lp, &pos,
                                              byte_counts + c * height,
                                              height, width,
                                              rgba + c, stride);
            else
                ok = psd_decode_rle_channel_16(data, len, lp, &pos,
                                               byte_counts + c * height,
                                               height, width,
                                               rgba + c, stride);
        }
        g_free(byte_counts);
        if (!ok) { g_free(rgba); return NULL; }
    }

    if (g_atomic_int_get(&lp->abort))
    {
        g_free(rgba);
        return NULL;
    }

    GdkPixbuf *full = gdk_pixbuf_new_from_data(rgba, GDK_COLORSPACE_RGB, has_alpha, 8,
                                               width, height, width * stride,
                                               (GdkPixbufDestroyNotify) g_free, NULL);
    return full;
}

/* Lightweight header-only parse, used to learn native width/height before
 * doing any real decode work, so we can fire size_cb() early the same way
 * image_loader_jpeg_load() does -- this is what allows image-load.c's
 * image_loader_size_cb() to call our set_size() with a downscale target
 * when a thumbnail (not a full view) was requested. */
static gboolean psd_read_header(const guchar *data, gsize len, PsdHeader *header_out)
{
    gsize pos = 0;

    if (len < 26 || memcmp(data, "8BPS", 4) != 0) return FALSE;
    pos += 4;

    header_out->version    = psd_read_u16(data + pos); pos += 2;

    if (!(header_out->version == 1 ||
          header_out->version == 2))
        return FALSE;
    pos += 6; /* reserved */

    header_out->channels   = psd_read_u16(data + pos); pos += 2;
    header_out->height     = psd_read_u32(data + pos); pos += 4;
    header_out->width      = psd_read_u32(data + pos); pos += 4;
    header_out->depth      = psd_read_u16(data + pos); pos += 2;
    header_out->color_mode = psd_read_u16(data + pos); pos += 2;

    return TRUE;
}

#define PSD_MAX_LAYER_CHANNELS 16

typedef struct PsdLayer PsdLayer;
struct PsdLayer {
    gint32  top, left, bottom, right;
    guint   nchannels;                      /* stored, clamped to the max */
    gint16  chan_id[PSD_MAX_LAYER_CHANNELS];
    guint64 chan_len[PSD_MAX_LAYER_CHANNELS];
    guint64 data_off;                       /* absolute offset of first channel's data */
};

/* Offset of the Layer and Mask Information section (just past the Image
 * Resources section). Assumes psd_read_header() already checked len >= 26. */
static gboolean psd_layer_section_pos(const guchar *data, gsize len, gsize *pos_out)
{
    gsize pos = 26;
    if (len - pos < 4) return FALSE;
    guint32 n = psd_read_u32(data + pos); pos += 4;
    if (n > len - pos) return FALSE;
    pos += n;                               /* color mode data */

    if (len - pos < 4) return FALSE;
    n = psd_read_u32(data + pos); pos += 4;
    if (n > len - pos) return FALSE;
    *pos_out = pos + n;                     /* image resources */
    return TRUE;
}

static gboolean psd_layer_drawable(const PsdLayer *L, guint32 limit)
{
    gint64 w = (gint64) L->right - L->left;
    gint64 h = (gint64) L->bottom - L->top;
    if (w <= 0 || h <= 0 || w > limit || h > limit) return FALSE;

    guint have = 0;
    for (guint c = 0; c < L->nchannels; c++)
        if (L->chan_id[c] >= 0 && L->chan_id[c] <= 2)
            have |= 1u << L->chan_id[c];
    return have == 7;
}

/* Parse the "extra data" block of one PSD layer record and extract the
 * layer name. Layout within extra:
 *   [4] mask data length         -> skip 4 + len
 *   [4] blending ranges length   -> skip 4 + len
 *   [1] pascal name len          -> name bytes, padded to multiple of 4
 *   remaining: additional layer info blocks, each
 *     [4] "8BIM"/"8B64"  [4] key  [4] len  [len] data (padded to even)
 * We prefer the Unicode name from the 'luni' block (UTF-16BE) when it
 * exists, falling back to the Pascal string (interpreted as Latin-1,
 * which is what the spec effectively amounts to for non-MacRoman names).
 * Returns NULL if no usable name is found. */
static gchar *psd_layer_name_from_extra(const guchar *extra, gsize extra_len)
{
    gsize pos = 0;

    if (extra_len < 4) return NULL;
    guint32 mask_len = psd_read_u32(extra + pos); pos += 4;
    if (pos + mask_len > extra_len) return NULL;
    pos += mask_len;

    if (pos + 4 > extra_len) return NULL;
    guint32 blend_len = psd_read_u32(extra + pos); pos += 4;
    if (pos + blend_len > extra_len) return NULL;
    pos += blend_len;

    gchar *pascal_name = NULL;
    if (pos < extra_len)
    {
        guint8 name_len = extra[pos];
        if (name_len > 0 && pos + 1 + name_len <= extra_len)
            pascal_name = g_convert((const gchar *)extra + pos + 1, name_len,
                                    "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
        /* pascal string field is padded to a multiple of 4 bytes total */
        gsize field_len = 1 + name_len;
        field_len = (field_len + 3) & ~(gsize)3;
        pos += field_len;
    }

    gchar *unicode_name = NULL;
    while (pos + 12 <= extra_len)
    {
        if (memcmp(extra + pos, "8BIM", 4) != 0 &&
            memcmp(extra + pos, "8B64", 4) != 0)
            break;
        guint32 key = psd_read_u32(extra + pos + 4);
        guint32 blk_len = psd_read_u32(extra + pos + 8);
        gsize data_start = pos + 12;
        if (data_start + blk_len > extra_len) break;

        if (key == 0x6C756E69 /* 'luni' */ && blk_len >= 4)
        {
            guint32 ulen = psd_read_u32(extra + data_start);
            if (ulen > 0 && data_start + 4 + (gsize)ulen * 2 <= extra_len)
            {
                gunichar2 *u = g_new(gunichar2, ulen);
                for (guint32 i = 0; i < ulen; i++)
                    u[i] = psd_read_u16(extra + data_start + 4 + i * 2);
                unicode_name = g_utf16_to_utf8(u, ulen, NULL, NULL, NULL);
                g_free(u);
            }
        }

        pos = data_start + blk_len;
        if (blk_len % 2 != 0) pos += 1; /* additional info blocks pad to even */
    }

    if (unicode_name) { g_free(pascal_name); return unicode_name; }
    return pascal_name;
}

/* Walks the layer records. *total receives the number of drawable layers.
 * If want >= 0, *out receives the want-th drawable layer in record order
 * (bottom-up) and FALSE is returned if there is no such layer.
 * If names_out is non-NULL, *names_out receives a newly allocated array of
 * gchar* (length == *total), indexed by drawable order; NULL entries for
 * layers with no parseable name. Caller frees with g_strfreev(). */
static gboolean psd_walk_layers(const guchar *data, gsize len, gsize pos,
                                const PsdHeader *h, gint want,
                                PsdLayer *out, guint *total,
                                gchar ***names_out)
{
    const gsize lf = h->version == 1 ? 4 : 8;
    const guint32 limit = h->version == 1 ? 30000 : 300000;
    guint64 v;

    if (total)     *total = 0;
    if (names_out) *names_out = NULL;

    if (pos > len || len - pos < lf) return FALSE;
    v = lf == 4 ? psd_read_u32(data + pos) : psd_read_u64(data + pos);
    pos += lf;
    if (v == 0 || v > len - pos) return FALSE;      /* no layer data */

    if (len - pos < lf + 2) return FALSE;
    v = lf == 4 ? psd_read_u32(data + pos) : psd_read_u64(data + pos);
    pos += lf;
    if (v < 2 || v > len - pos) return FALSE;
    const gsize end = pos + v;                      /* end of layer info */

    const gint count = abs((gint16) psd_read_u16(data + pos));
    pos += 2;

    guint ndraw = 0;
    guint64 rel = 0;                                /* channel data, relative */
    gboolean found = FALSE;
    gchar **names = NULL;

    for (gint i = 0; i < count; i++)
    {
        PsdLayer L = { 0 };

        if (end - pos < 18) return FALSE;
        L.top    = (gint32) psd_read_u32(data + pos);
        L.left   = (gint32) psd_read_u32(data + pos + 4);
        L.bottom = (gint32) psd_read_u32(data + pos + 8);
        L.right  = (gint32) psd_read_u32(data + pos + 12);
        guint nch = psd_read_u16(data + pos + 16);
        pos += 18;

        if ((guint64) nch * (2 + lf) > end - pos) return FALSE;

        guint64 chan_total = 0;
        for (guint c = 0; c < nch; c++)
        {
            gint16 id = (gint16) psd_read_u16(data + pos);
            guint64 clen = lf == 4 ? psd_read_u32(data + pos + 2)
                                   : psd_read_u64(data + pos + 2);
            pos += 2 + lf;
            if (clen > len) return FALSE;
            if (c < PSD_MAX_LAYER_CHANNELS)
            {
                L.chan_id[c]  = id;
                L.chan_len[c] = clen;
                L.nchannels   = c + 1;
            }
            chan_total += clen;
        }

        /* blend sig + key (8), opacity/clip/flags/filler (4), extra length (4) */
        if (end - pos < 16) return FALSE;
        pos += 12;
        guint32 extra = psd_read_u32(data + pos); pos += 4;
        if (extra > end - pos) return FALSE;
        gchar *name = NULL;
        if (names_out)
            name = psd_layer_name_from_extra(data + pos, extra);
        pos += extra;               /* mask data, blend ranges, name, extra blocks */

        L.data_off = rel;
        rel += chan_total;

        if (psd_layer_drawable(&L, limit))
        {
            if (names_out)
            {
                if (!names) *names_out = names = g_new0(gchar *, count);
                /* we want this one so don't free it */
                names[ndraw] = g_steal_pointer(&name);
            }
            if (want >= 0 && (guint) want == ndraw && out)
            {
                *out = L;
                found = TRUE;
            }
            ndraw++;
        }
        g_free(name);
    }

    if (rel > end - pos) return FALSE;
    if (found) out->data_off += pos;                /* pos == end of records */
    if (total) *total = ndraw;
    if (names_out) *names_out = names;
    return want < 0 || found;
}

static GdkPixbuf *psd_decode_layer(const guchar *data, gsize len,
                                   ImageLoaderPsd *lp, const PsdHeader *h,
                                   const PsdLayer *L)
{
    if (h->color_mode != 3 || (h->depth != 8 && h->depth != 16)) return NULL;

    const guint32 w   = (guint32) ((gint64) L->right  - L->left);
    const guint32 ht  = (guint32) ((gint64) L->bottom - L->top);
    const guint32 bps = h->depth / 8;

    gboolean has_alpha = FALSE;
    for (guint c = 0; c < L->nchannels; c++)
        if (L->chan_id[c] == -1) has_alpha = TRUE;
    const guint32 stride = has_alpha ? 4 : 3;

    guint64 size = (guint64) w * ht * stride;
    if (size > 1024ULL * 1024 * 1024) return NULL;
    guchar *px = g_try_malloc(size);
    if (!px) return NULL;
    memset(px, 255, size);

    guint64 off = L->data_off;
    for (guint c = 0; c < L->nchannels; c++)
    {
        const gint id = L->chan_id[c];
        const gint plane = (id >= 0 && id <= 2) ? id : (id == -1 ? 3 : -1);
        const guint64 clen = L->chan_len[c];
        gsize pos = off;
        off += clen;
        if (plane < 0) continue;                    /* user masks etc. */

        if (clen < 2 || off > len) goto fail;
        guint16 comp = psd_read_u16(data + pos); pos += 2;

        if (comp == 0)                              /* raw */
        {
            if (clen - 2 < (guint64) w * ht * bps) goto fail;
            for (guint32 y = 0; y < ht; y++)
            {
                if (g_atomic_int_get(&lp->abort)) goto fail;
                guchar *d = px + (gsize) y * w * stride + plane;
                const guchar *s = data + pos + (gsize) y * w * bps;
                for (guint32 x = 0; x < w; x++)
                    d[(gsize) x * stride] = s[(gsize) x * bps];     /* MSB */
            }
        }
        else if (comp == 1)                         /* PackBits */
        {
            if (clen - 2 < (guint64) ht * 2 * h->version) goto fail;
            guint32 *counts = g_try_new(guint32, ht);
            if (!counts) goto fail;
            for (guint32 y = 0; y < ht; y++)
            {
                if (h->version == 1) { counts[y] = psd_read_u16(data + pos); pos += 2; }
                else                 { counts[y] = psd_read_u32(data + pos); pos += 4; }
            }
            gboolean ok = bps == 1
                ? psd_decode_rle_channel_8 (data, len, lp, &pos, counts, ht, w, px + plane, stride)
                : psd_decode_rle_channel_16(data, len, lp, &pos, counts, ht, w, px + plane, stride);
            g_free(counts);
            if (!ok) goto fail;
        }
        else
            goto fail;                              /* ZIP (2, 3) unsupported */
    }

    return gdk_pixbuf_new_from_data(px, GDK_COLORSPACE_RGB, has_alpha, 8,
                                    w, ht, (gsize) w * stride,
                                    (GdkPixbufDestroyNotify) g_free, NULL);
fail:
    g_free(px);
    return NULL;
}

static gboolean image_loader_psd_load(gpointer loader, const guchar *buf,
                                      gsize count, GError **error)
{
    ImageLoaderPsd *lp = (ImageLoaderPsd *)loader;

    PsdHeader header;
    gsize resources_end = 0;
    GdkPixbuf *embedded_thumb;

    if (g_atomic_int_get(&lp->abort)) return FALSE;

    if (!psd_read_header(buf, count, &header))
    {
        if (error) g_set_error(error, GDK_PIXBUF_ERROR, 0, "psd: invalid header");
        return FALSE;
    }

    /* Seed requested_width/height with the native size, then fire size_cb
     * early -- exactly like image_loader_jpeg_load() does before decoding.
     * If this is a full-view (non-thumbnail) load, image_loader_size_cb()
     * returns early and these stay at native size. If it's a thumbnail
     * request, image_loader_size_cb() now recognizes our "photoshop" mime
     * type and calls image_loader_psd_set_size() with the aspect-correct
     * target size, overwriting these before we read them below. */
    lp->requested_width  = header.width;
    lp->requested_height = header.height;
    lp->is_psb = header.version == 2;
    if (lp->size_cb) lp->size_cb(lp, header.width, header.height, lp->data);

    ImageLoader *il = (ImageLoader *) lp->data;
    guint page = (il && il->fd) ? il->fd->page_num : 0;
    gsize layers_pos = 0;
    guint nlayers = 0;
    gchar **names = NULL;

    if (psd_layer_section_pos(buf, count, &layers_pos))
        if (!psd_walk_layers(buf, count, layers_pos, &header, -1, NULL, &nlayers,
                             il && il->fd ? &names : NULL))
            g_strfreev(names);

    if (il && il->fd)
    {
        g_clear_pointer(&il->fd->page_names, g_strfreev);
        /* names_out[] is in file layer order; page order is reversed:
         * page 0 = composite, page i>0 = layer (nlayers - i) */
        if (names)
        {
            il->fd->page_names = g_new0(gchar *, nlayers + 2);
            il->fd->page_names[0] = g_strdup(_("Preview"));
            for (guint i = 1; i <= nlayers; i++)
                il->fd->page_names[i] = g_steal_pointer(&names[nlayers - i]); /* may be NULL */
        }
        il->fd->page_total = 1 + nlayers;
    }
    g_free(names);

    if (page > 0 && page <= nlayers)
    {
        PsdLayer layer;
        if (psd_walk_layers(buf, count, layers_pos, &header,
                            (gint) (nlayers - page), &layer, NULL, NULL))
        {
            /* report the layer's size, not the canvas size */
            if (lp->size_cb)
                lp->size_cb(lp, layer.right - layer.left,
                            layer.bottom - layer.top, lp->data);

            lp->pixbuf = psd_decode_layer(buf, count, lp, &header, &layer);
            if (!lp->pixbuf)
            {
                if (error && !g_atomic_int_get(&lp->abort))
                    g_set_error(error, GDK_PIXBUF_ERROR, 0, "psd: cannot decode layer %u", page);
                return FALSE;
            }
            if (lp->area_updated_cb)
                lp->area_updated_cb(lp, 0, 0, gdk_pixbuf_get_width(lp->pixbuf),
                                    gdk_pixbuf_get_height(lp->pixbuf), lp->data);
            return TRUE;
        }
    }

    /* Decide which image to use for page 0 */

    /* Parse the resources section regardless, since we need `resources_end`
     * either way -- but don't decide whether to use the returned
     * embedded-thumbnail pixbuf until we know the requested size. */
    embedded_thumb = psd_extract_embedded_thumbnail(buf, count, lp,
                                                    &resources_end);

    /* Prefer the thumbnail if it satisfies the requested width, otherwise use
     * the full size composite image */
    if (embedded_thumb &&
        gdk_pixbuf_get_width(embedded_thumb)  >= (gint) lp->requested_width &&
        gdk_pixbuf_get_height(embedded_thumb) >= (gint) lp->requested_height)
    {
        DEBUG_1("psd: embedded thumbnail is large enough for the request, skipping composite decode");
        lp->pixbuf = embedded_thumb;
    }
    else if (resources_end > 0)
    {
        DEBUG_1("psd: decoding full composite image data");
        lp->pixbuf = psd_decode_composite(buf, count, lp, resources_end, &header);

        if (lp->pixbuf)
        {
            g_clear_object(&embedded_thumb);
        }
        else if (embedded_thumb)
        {
            DEBUG_1("psd: composite decode failed/unsupported, falling back to embedded thumbnail");
            lp->pixbuf = embedded_thumb;
        }
    }
    else if (embedded_thumb)
    {
        lp->pixbuf = embedded_thumb;
    }

    if (!lp->pixbuf)
    {
        gboolean aborted = g_atomic_int_get(&lp->abort);
        if (error && !aborted)
            g_set_error(error, GDK_PIXBUF_ERROR, 0, "psd: no decodable image data found");
        return FALSE;
    }

    if (lp->area_updated_cb)
        lp->area_updated_cb(lp, 0, 0,
                            gdk_pixbuf_get_width(lp->pixbuf),
                            gdk_pixbuf_get_height(lp->pixbuf), lp->data);

    return TRUE;
}

static gboolean image_loader_psd_close(gpointer loader, GError **error)
{
    return TRUE;
}

static GdkPixbuf *image_loader_psd_get_pixbuf(gpointer loader)
{
    ImageLoaderPsd *lp = (ImageLoaderPsd *)loader;
    return lp->pixbuf;
}

static gchar *image_loader_psd_get_format_name(gpointer loader)
{
    ImageLoaderPsd *lp = (ImageLoaderPsd *)loader;
    return g_strdup(lp->is_psb ? "psb" : "psd");
}

static gchar **image_loader_psd_get_format_mime_types(gpointer loader)
{
    static gchar *mime[] = { "image/vnd.adobe.photoshop", NULL };
    return g_strdupv(mime);
}

static void image_loader_psd_abort(gpointer loader)
{
    ImageLoaderPsd *lp = (ImageLoaderPsd *)loader;
    g_atomic_int_set(&lp->abort, TRUE);
}

static void image_loader_psd_free(gpointer loader)
{
    ImageLoaderPsd *lp = (ImageLoaderPsd *)loader;
    g_clear_object(&lp->pixbuf);
    g_free(lp);
}

void image_loader_backend_set_psd(ImageLoaderBackend *funcs)
{
    funcs->loader_new = image_loader_psd_new;
    funcs->set_size   = image_loader_psd_set_size;
    funcs->load       = image_loader_psd_load;
    funcs->write      = NULL;
    funcs->get_pixbuf = image_loader_psd_get_pixbuf;
    funcs->close      = image_loader_psd_close;
    funcs->abort      = image_loader_psd_abort;
    funcs->free       = image_loader_psd_free;

    funcs->get_format_name       = image_loader_psd_get_format_name;
    funcs->get_format_mime_types = image_loader_psd_get_format_mime_types;
}
