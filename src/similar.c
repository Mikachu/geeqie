/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
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

#include <math.h>

#include "main.h"
#include "similar.h"

/*
 * These functions are intended to find images with similar color content. For
 * example when an image was saved at different compression levels or dimensions
 * (scaled down/up) the contents are similar, but these files do not match by file
 * size, dimensions, or checksum.
 *
 * These functions create a 32 x 32 array for each color channel (red, green, blue).
 * The array represents the average color of each corresponding part of the
 * image. (imagine the image cut into 1024 rectangles, or a 32 x 32 grid.
 * Each grid is then processed for the average color value, this is what
 * is stored in the array)
 *
 * To compare two images, generate a ImageSimilarityData for each image, then
 * pass them to the compare function. The return value is the percent match
 * of the two images. (for this, simple comparisons are used, basically the return
 * is an average of the corresponding array differences)
 *
 * for image_sim_compare(), the return is 0.0 to 1.0:
 *  1.0 for exact matches (an image is compared to itself)
 *  0.0 for exact opposite images (compare an all black to an all white image)
 * generally only a match of > 0.85 are significant at all, and >.95 is useful to
 * find images that have been re-saved to other formats, dimensions, or compression.
 */


/*
 * The experimental (alternate) algorithm is only for testing of new techniques to
 * improve the result, and hopes to reduce false positives.
 */

static gboolean alternate_enabled = FALSE;

void image_sim_alternate_set(gboolean enable)
{
    alternate_enabled = enable;
}

gboolean image_sim_alternate_enabled(void)
{
    return alternate_enabled;
}

ImageSimilarityData *image_sim_new(void)
{
    ImageSimilarityData *sd = g_new0(ImageSimilarityData, 1);

    return sd;
}

void image_sim_free(ImageSimilarityData *sd)
{
    g_free(sd);
}

static gint image_sim_channel_eq_sort_cb(gconstpointer a, gconstpointer b)
{
    gint *pa = (gpointer)a;
    gint *pb = (gpointer)b;
    if (pa[1] < pb[1]) return -1;
    if (pa[1] > pb[1]) return 1;
    return 0;
}

static void image_sim_channel_equal(guint8 *pix, gint len)
{
    gint *buf;
    gint i;
    gint p;

    buf = g_new0(gint, len * 2);

    p = 0;
    for (i = 0; i < len; i++)
    {
        buf[p] = i;
        p++;
        buf[p] = (gint)pix[i];
        p++;
    }

    qsort(buf, len, sizeof(gint) * 2, image_sim_channel_eq_sort_cb);

    p = 0;
    for (i = 0; i < len; i++)
    {
        gint n;

        n = buf[p];
        p += 2;
        pix[n] = (guint8)(255 * i / len);
    }

    g_free(buf);
}

static void image_sim_channel_norm(guint8 *pix, gint len)
{
    guint8 l, h, delta;
    gint i;
    gdouble scale;

    l = h = pix[0];

    for (i = 0; i < len; i++)
    {
        if (pix[i] < l) l = pix[i];
        if (pix[i] > h) h = pix[i];
    }

    delta = h - l;
    scale = (delta != 0) ? 255.0 / (gdouble)(delta) : 1.0;

    for (i = 0; i < len; i++)
    {
        pix[i] = (guint8)((gdouble)(pix[i] - l) * scale);
    }
}

/*
 * define these to enable various components of the experimental compare functions
 *
 * Convert the thumbprint to greyscale (ignore all color information when comparing)
 *  #define ALTERNATE_USES_GREYSCALE 1
 *
 * Take into account the difference in change from one pixel to the next
 *  #define ALTERNATE_INCLUDE_COMPARE_CHANGE 1
 */

void image_sim_alternate_processing(ImageSimilarityData *sd)
{
#ifdef ALTERNATE_USES_GREYSCALE
    gint i;
#endif

    if (!alternate_enabled) return;

    image_sim_channel_norm(sd->avg_r, sizeof(sd->avg_r));
    image_sim_channel_norm(sd->avg_g, sizeof(sd->avg_g));
    image_sim_channel_norm(sd->avg_b, sizeof(sd->avg_b));

    image_sim_channel_equal(sd->avg_r, sizeof(sd->avg_r));
    image_sim_channel_equal(sd->avg_g, sizeof(sd->avg_g));
    image_sim_channel_equal(sd->avg_b, sizeof(sd->avg_b));

#ifdef ALTERNATE_USES_GREYSCALE
    for (i = 0; i < sizeof(sd->avg_r); i++)
    {
        guint8 n;

        n = (guint8)((gint)(sd->avg_r[i] + sd->avg_g[i] + sd->avg_b[i]) / 3);
        sd->avg_r[i] = sd->avg_g[i] = sd->avg_b[i] = n;
    }
#endif
}

void image_sim_calc_phash(ImageSimilarityData *sd)
{
    if (!sd || !sd->filled) return;

    /* Precomputed cosine table: cos_table[n][k] = cosf(M_PI * (2*n+1) * k / 64.0f) */
    static float cos_table[32][8];
    static gboolean cos_table_init = FALSE;
    if (!cos_table_init)
    {
        for (int n = 0; n < 32; n++)
            for (int k = 0; k < 8; k++)
                cos_table[n][k] = cosf((float)M_PI * (2*n+1) * k / 64.0f);
        cos_table_init = TRUE;
    }

    /* Convert to grayscale (luminance) */
    float gray[1024];
    for (int i = 0; i < 1024; i++)
        gray[i] = 0.299f * sd->avg_r[i] + 0.587f * sd->avg_g[i] + 0.114f * sd->avg_b[i];

    /* Compute 8x8 top-left block of the 2D DCT */
    float dct[8][8];
    for (int u = 0; u < 8; u++)
        for (int v = 0; v < 8; v++)
        {
            float sum = 0.0f;
            for (int y = 0; y < 32; y++)
            {
                float row_sum = 0.0f;
                for (int x = 0; x < 32; x++)
                    row_sum += gray[y*32+x] * cos_table[x][u];
                sum += row_sum * cos_table[y][v];
            }
            dct[u][v] = sum;
        }

    /* Collect 63 non-DC coefficients (skip dct[0][0]) */
    float vals[63];
    int n = 0;
    for (int u = 0; u < 8; u++)
        for (int v = 0; v < 8; v++)
            if (u != 0 || v != 0)
                vals[n++] = dct[u][v];

    /* Find median via insertion sort of a copy */
    float sorted[63];
    for (int i = 0; i < 63; i++) sorted[i] = vals[i];
    for (int i = 1; i < 63; i++)
    {
        float key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) { sorted[j+1] = sorted[j]; j--; }
        sorted[j+1] = key;
    }
    float median = sorted[31]; /* middle of 63 elements */

    /* Build 63-bit hash: bit i set if vals[i] > median */
    guint64 hash = 0;
    for (int i = 0; i < 63; i++)
    {
        hash <<= 1;
        if (vals[i] > median) hash |= 1;
    }

    sd->phash = hash;
    sd->phash_filled = TRUE;
}

static gint sim_phash_vp_entry_dist(gconstpointer a, gconstpointer b)
{
    const SimVPEntry *ea = a;
    const SimVPEntry *eb = b;
    return __builtin_popcountll(ea->phash ^ eb->phash);
}

VPTree *image_sim_phash_vptree_build(GList *entries)
{
    return vptree_build(entries, sim_phash_vp_entry_dist);
}

void image_sim_calc_coarse(ImageSimilarityData *sd)
{
    for (int cy = 0; cy < 4; cy++) {
        for (int cx = 0; cx < 4; cx++) {
            gint r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 8; dy++) {
                for (int dx = 0; dx < 8; dx++) {
                    gint t = (cy*8+dy)*32 + (cx*8+dx);
                    r += sd->avg_r[t]; g += sd->avg_g[t]; b += sd->avg_b[t];
                }
            }
            sd->coarse_r[cy*4+cx] = r / 64;
            sd->coarse_g[cy*4+cx] = g / 64;
            sd->coarse_b[cy*4+cx] = b / 64;
        }
    }
    sd->coarse_filled = TRUE;
}

void image_sim_coarse_rot(ImageSimilarityData *sd, gint transfo,
                          guint8 out_r[16], guint8 out_g[16], guint8 out_b[16])
{
    for (gint cy = 0; cy < 4; cy++)
    {
        for (gint cx = 0; cx < 4; cx++)
        {
            gint si, sj;
            if (transfo & 1) { si = cx; sj = cy; }
            else             { si = cy; sj = cx; }
            if (transfo & 2) sj = 3 - sj;
            if (transfo & 4) si = 3 - si;
            gint src = si * 4 + sj;
            gint dst = cy * 4 + cx;
            out_r[dst] = sd->coarse_r[src];
            out_g[dst] = sd->coarse_g[src];
            out_b[dst] = sd->coarse_b[src];
        }
    }
}

static gint sim_vp_entry_dist(gconstpointer a, gconstpointer b)
{
    const SimVPEntry *ea = a;
    const SimVPEntry *eb = b;
    gint d = 0;
    for (gint i = 0; i < 16; i++)
    {
        d += abs(ea->coarse_r[i] - eb->coarse_r[i]);
        d += abs(ea->coarse_g[i] - eb->coarse_g[i]);
        d += abs(ea->coarse_b[i] - eb->coarse_b[i]);
    }
    return d;
}

VPTree *image_sim_vptree_build(GList *entries)
{
    return vptree_build(entries, sim_vp_entry_dist);
}

GList *image_sim_vptree_query(VPTree *tree, SimVPEntry *query, gint radius)
{
    return vptree_range_query(tree, query, radius);
}

void image_sim_fill_data(ImageSimilarityData *sd, GdkPixbuf *pixbuf)
{
    gint w, h;
    gint rs;
    guchar *pix;
    gboolean has_alpha;
    gint p_step;

    guchar *p;
    gint i;
    gint j;
    gint x_inc, y_inc, xy_inc;
    gint xs, ys;
    gint w_left, h_left;

    gboolean x_small = FALSE;   /* if less than 32 w or h, set TRUE */
    gboolean y_small = FALSE;
    if (!sd || !pixbuf) return;

    w = gdk_pixbuf_get_width(pixbuf);
    h = gdk_pixbuf_get_height(pixbuf);
    rs = gdk_pixbuf_get_rowstride(pixbuf);
    pix = gdk_pixbuf_get_pixels(pixbuf);
    has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);

    p_step = has_alpha ? 4 : 3;
    x_inc = w / 32;
    y_inc = h / 32;
    w_left = w;
    h_left = h;

    if (x_inc < 1)
    {
        x_inc = 1;
        x_small = TRUE;
    }
    if (y_inc < 1)
    {
        y_inc = 1;
        y_small = TRUE;
    }

    j = 0;

    h_left = h;
    for (ys = 0; ys < 32; ys++)
    {
        if (y_small) j = (gfloat)h / 32 * ys;
                else y_inc = (gint)roundf((gfloat)h_left/(32-ys));
        i = 0;

        w_left = w;
        for (xs = 0; xs < 32; xs++)
        {
            gint x, y;
            gint r, g, b;
            gint t;
            guchar *xpos;

            if (x_small) i = (gfloat)w / 32 * xs;
                    else x_inc = (gint)roundf((gfloat)w_left/(32-xs));
            xy_inc = x_inc * y_inc;
            r = g = b = 0;
            xpos = pix + (i * p_step);

            for (y = j; y < j + y_inc; y++)
            {
                p = xpos + (y * rs);
                for (x = i; x < i + x_inc; x++)
                {
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    p += p_step;
                }
            }

            r /= xy_inc;
            g /= xy_inc;
            b /= xy_inc;

            t = ys * 32 + xs;
            sd->avg_r[t] = r;
            sd->avg_g[t] = g;
            sd->avg_b[t] = b;

            i += x_inc;
            w_left -= x_inc;
        }

        j += y_inc;
        h_left -= y_inc;
    }

    sd->filled = TRUE;
}

ImageSimilarityData *image_sim_new_from_pixbuf(GdkPixbuf *pixbuf)
{
    ImageSimilarityData *sd;

    sd = image_sim_new();
    image_sim_fill_data(sd, pixbuf);

    return sd;
}

#ifdef ALTERNATE_INCLUDE_COMPARE_CHANGE
static gdouble alternate_image_sim_compare_fast(ImageSimilarityData *a, ImageSimilarityData *b, gdouble min)
{
    gint sim;
    gint i;
    gint j;
    gint ld;

    if (!a || !b || !a->filled || !b->filled) return 0.0;

    min = 1.0 - min;
    sim = 0.0;
    ld = 0;

    for (j = 0; j < 1024; j += 32)
    {
        for (i = j; i < j + 32; i++)
        {
            gint cr, cg, cb;
            gint cd;

            cr = abs(a->avg_r[i] - b->avg_r[i]);
            cg = abs(a->avg_g[i] - b->avg_g[i]);
            cb = abs(a->avg_b[i] - b->avg_b[i]);

            cd = cr + cg + cb;
            sim += cd + abs(cd - ld);
            ld = cd / 3;
        }
        /* check for abort, if so return 0.0 */
        if ((gdouble)sim / (255.0 * 1024.0 * 4.0) > min) return 0.0;
    }

    return (1.0 - ((gdouble)sim / (255.0 * 1024.0 * 4.0)) );
}
#endif

gdouble image_sim_compare_transfo(ImageSimilarityData *a, ImageSimilarityData *b, gchar transfo)
{
    gint sim;
    gint i1, i2, *i;
    gint j1, j2, *j;

    if (!a || !b || !a->filled || !b->filled) return 0.0;

    sim = 0.0;

    if (transfo & 1) { i = &j2; j = &i2; } else { i = &i2; j = &j2; }
    for (j1 = 0; j1 < 32; j1++)
    {
        if (transfo & 2) *j = 31-j1; else *j = j1;
        for (i1 = 0; i1 < 32; i1++)
        {
            if (transfo & 4) *i = 31-i1; else *i = i1;
            sim += abs(a->avg_r[i1*32+j1] - b->avg_r[i2*32+j2]);
            sim += abs(a->avg_g[i1*32+j1] - b->avg_g[i2*32+j2]);
            sim += abs(a->avg_b[i1*32+j1] - b->avg_b[i2*32+j2]);
        }
    }

    return 1.0 - ((gdouble)sim / (255.0 * 1024.0 * 3.0));
}

gdouble image_sim_compare(ImageSimilarityData *a, ImageSimilarityData *b)
{
    gint max_t = (options->rot_invariant_sim ? 8 : 1);

    gint t;
    gdouble score, max_score = 0;

    for(t = 0; t < max_t; t++)
    {
        score = image_sim_compare_transfo(a, b, t);
        if (score > max_score) max_score = score;
    }
    return max_score;
}


/*
4 rotations (0, 90, 180, 270) combined with two mirrors (0, H)
generate all possible isometric transformations
= 8 tests
= change dir of x, change dir of y, exchange x and y = 2^3 = 8
*/

/* Non-transpose: i1 outer, j1 inner — both arrays sequential/reverse-sequential */
#define DEFINE_TRANSFO_NT(N, BIDX)                                         \
static gdouble image_sim_compare_fast_##N(                                 \
    ImageSimilarityData *a, ImageSimilarityData *b, gint max_sim)          \
{                                                                          \
    gint sim = 0, i1, j1;                                                  \
    for (i1 = 0; i1 < 32; i1++)                                            \
        {                                                                  \
        for (j1 = 0; j1 < 32; j1++)                                        \
            {                                                              \
            sim += abs(a->avg_r[i1*32+j1] - b->avg_r[BIDX]);               \
            sim += abs(a->avg_g[i1*32+j1] - b->avg_g[BIDX]);               \
            sim += abs(a->avg_b[i1*32+j1] - b->avg_b[BIDX]);               \
            }                                                              \
        if (sim > max_sim) return 0.0;                                     \
        }                                                                  \
    return 1.0 - ((gdouble)sim / (255.0 * 1024.0 * 3.0));                  \
}

/* Transpose: j1 outer, i1 inner — b sequential, a stride-32 */
#define DEFINE_TRANSFO_T(N, BIDX)                                          \
static gdouble image_sim_compare_fast_##N(                                 \
    ImageSimilarityData *a, ImageSimilarityData *b, gint max_sim)          \
{                                                                          \
    gint sim = 0, i1, j1;                                                  \
    for (j1 = 0; j1 < 32; j1++)                                            \
        {                                                                  \
        for (i1 = 0; i1 < 32; i1++)                                        \
            {                                                              \
            sim += abs(a->avg_r[i1*32+j1] - b->avg_r[BIDX]);               \
            sim += abs(a->avg_g[i1*32+j1] - b->avg_g[BIDX]);               \
            sim += abs(a->avg_b[i1*32+j1] - b->avg_b[BIDX]);               \
            }                                                              \
        if (sim > max_sim) return 0.0;                                     \
        }                                                                  \
    return 1.0 - ((gdouble)sim / (255.0 * 1024.0 * 3.0));                  \
}

DEFINE_TRANSFO_NT(0, i1*32+j1)
DEFINE_TRANSFO_T( 1, j1*32+i1)
DEFINE_TRANSFO_NT(2, i1*32+(31-j1))
DEFINE_TRANSFO_T( 3, (31-j1)*32+i1)
DEFINE_TRANSFO_NT(4, (31-i1)*32+j1)
DEFINE_TRANSFO_T( 5, j1*32+(31-i1))
DEFINE_TRANSFO_NT(6, (31-i1)*32+(31-j1))
DEFINE_TRANSFO_T( 7, (31-j1)*32+(31-i1))

/* this uses a cutoff point so that it can abort early when it gets to
 * a point that can simply no longer make the cut-off point.
 */

typedef gdouble (*transfo_fn)(ImageSimilarityData *, ImageSimilarityData *, gint);
static const transfo_fn transfo_funcs[8] = {
    image_sim_compare_fast_0, image_sim_compare_fast_6,
    image_sim_compare_fast_2, image_sim_compare_fast_4,
    image_sim_compare_fast_1, image_sim_compare_fast_3,
    image_sim_compare_fast_5, image_sim_compare_fast_7,
};

gdouble image_sim_compare_fast(ImageSimilarityData *a, ImageSimilarityData *b, gdouble min)
{
    if (!a || !b || !a->filled || !b->filled) return 0.0;
    gint max_t = (options->rot_invariant_sim ? 8 : 1);
    gint max_sim = (gint)((1.0 - min) * (255.0 * 1024.0 * 3.0));
    gdouble score, max_score = 0.0;
    for (gint t = 0; t < max_t; t++)
        {
        score = transfo_funcs[t](a, b, max_sim);
        if (score > max_score)
            {
            max_score = score;
            max_sim = (gint)((1.0 - max_score) * (255.0 * 1024.0 * 3.0));
            }
        }
    return max_score;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
