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

ImageSimilarityData *image_sim_new(void)
{
    return g_new0(ImageSimilarityData, 1);
}

void image_sim_free(ImageSimilarityData *sd)
{
    g_free(sd);
}

/* Maps vals[] index n (scan order: u=0..7,v=0..7, skipping (0,0)) to the
 * index holding the transposed coefficient (v,u). Built once at first use. */
static guint8 phash_transpose_perm[63];
static gsize phash_transpose_perm_init = FALSE; /* only access through g_once_* */

static void phash_init_transpose_perm(void)
{
    gint idx[8][8];
    gint n = 0;
    for (int u = 0; u < 8; u++)
    for (int v = !u; v < 8; v++)
        idx[u][v] = n++;

    n = 0;
    for (int u = 0; u < 8; u++)
    for (int v = !u; v < 8; v++)
        phash_transpose_perm[n++] = idx[v][u];
}

/* Given a 63-bit hash built by image_sim_calc_phash()'s threshold loop
 * (vals[i] occupies bit i), returns the hash that would result from
 * applying the same threshold to a vals[] array permuted by perm[], i.e.
 * vals_new[i] = vals[perm[i]]. Since median/threshold only depend on the
 * *set* of values, and transpose is a pure index permutation of dct[][]
 * (no sign change), this is exact - no recomputation needed. */
static guint64 phash_permute_bits(guint64 h, const guint8 perm[63])
{
    guint64 out = 0;
    for (gint i = 0; i < 63; i++)
        out |= ((h >> perm[i]) & 1) << i;

    return out;
}

/* Computes the 63-bit median-threshold hash for one 8x8 coefficient grid. */
static guint64 phash_hash_from_dct(float dct[8][8])
{
    const float *vals = &dct[0][1];
    guint64 hash = 0;

    for (int i = 0; i < 63; i++)
    {
        int less = 0;
        for (int j = 0; j < 63; j++)
            less += vals[j] < vals[i];
        hash |= (guint64)(less >= 32) << i;
    }

    return hash;
}

void image_sim_calc_phash(ImageSimilarityData *sd)
{
    if (!sd || !sd->filled) return;

    if (g_once_init_enter(&phash_transpose_perm_init))
    {
        phash_init_transpose_perm();
        g_once_init_leave(&phash_transpose_perm_init, TRUE);
    }

    /* Precomputed cosine table: cos_table[n][k] = cosf(M_PI * (2*n+1) * k / 64.0f) */
    static float cos_table[32][8];
    static gsize cos_table_init = FALSE; /* only access through g_once_* */
    if (g_once_init_enter(&cos_table_init))
    {
        for (int n = 0; n < 32; n++)
        for (int k = 0; k < 8; k++)
            cos_table[n][k] = cosf((float)M_PI * (2*n+1) * k / 64.0f);
        g_once_init_leave(&cos_table_init, TRUE);
    }

    /* Convert to grayscale (luminance) */
    float gray[1024];
    for (int i = 0; i < 1024; i++)
        gray[i] = 0.299f * sd->avg_r[i] + 0.587f * sd->avg_g[i] + 0.114f * sd->avg_b[i];

    /* Compute 8x8 top-left block of the 2D DCT */

    /* Separable: transform each row once (keeping only the 8 lowest
     * horizontal frequencies), then transform the columns. */
    float rows[32][8] = { { 0 } };
    for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++)
    {
        float g = gray[y*32 + x];
        for (int u = 0; u < 8; u++)
            rows[y][u] += g * cos_table[x][u];
    }

    float dct[8][8] = { { 0 } };
    for (int y = 0; y < 32; y++)
    for (int u = 0; u < 8; u++)
    {
        float r = rows[y][u];
        for (int v = 0; v < 8; v++)
            dct[u][v] += r * cos_table[y][v];
    }

    /* Four "representative" sign variants of the same DCT grid: identity,
     * horizontal flip ((-1)^u), vertical flip ((-1)^v), 180-deg rotation
     * ((-1)^(u+v)). Each requires its own sort+threshold pass. */
    float dct_id[8][8], dct_h[8][8], dct_v[8][8], dct_r[8][8];
    for (int u = 0; u < 8; u++)
    for (int v = 0; v < 8; v++)
    {
        float val = dct[u][v];
        dct_id[u][v] = val;
        dct_h[u][v]  = (u & 1) ? -val : val;
        dct_v[u][v]  = (v & 1) ? -val : val;
        dct_r[u][v]  = ((u + v) & 1) ? -val : val;
    }

    guint64 h_id = phash_hash_from_dct(dct_id);
    guint64 h_h  = phash_hash_from_dct(dct_h);
    guint64 h_v  = phash_hash_from_dct(dct_v);
    guint64 h_r  = phash_hash_from_dct(dct_r);

    /* The remaining 4 orientations (transpose, 90, 270, anti-transpose) are
     * each exactly the matrix-transpose of one of the above grids, so their
     * hashes are obtained via a fixed bit permutation - no resort needed. */
    guint64 h_t    = phash_permute_bits(h_id, phash_transpose_perm); /* transpose */
    guint64 h_90   = phash_permute_bits(h_h,  phash_transpose_perm); /* 90 deg */
    guint64 h_270  = phash_permute_bits(h_v,  phash_transpose_perm); /* 270 deg */
    guint64 h_anti = phash_permute_bits(h_r,  phash_transpose_perm); /* anti-transpose */

    sd->phash[0] = h_id;
    sd->phash[1] = h_h;
    sd->phash[2] = h_v;
    sd->phash[3] = h_r;
    sd->phash[4] = h_t;
    sd->phash[5] = h_90;
    sd->phash[6] = h_270;
    sd->phash[7] = h_anti;

    sd->phash_filled = TRUE;
}

static gint sim_phash_vp_entry_dist(gconstpointer a, gconstpointer b)
{
    const SimVPHEntry *ea = a;
    const SimVPHEntry *eb = b;
    return __builtin_popcountll(ea->phash[0] ^ eb->phash[0]);
}

static gint sim_phash_vp_entry_dist_orb(gconstpointer a, gconstpointer b)
{
    const SimVPHEntry *ea = a;
    const SimVPHEntry *eb = b;
    /* Only ea's identity-orientation hash is needed: eb already carries all
     * 8 orientations, so the minimum over them covers every relative
     * rotation/reflection between the two images. */
    gint best = G_MAXINT;
    for (gint t = 0; t < 8; t++)
    {
        gint d = __builtin_popcountll(ea->phash[0] ^ eb->phash[t]);
        if (best > d)
            best = d;
    }
    return best;
}

VPTree *image_sim_phash_vptree_build(GList *entries, gboolean rot)
{
    return vptree_build(entries, rot ? sim_phash_vp_entry_dist_orb : sim_phash_vp_entry_dist);
}

void image_sim_calc_coarse(ImageSimilarityData *sd)
{
    for (int cy = 0; cy < 4; cy++)
    for (int cx = 0; cx < 4; cx++) {
        gint r = 0, g = 0, b = 0;
        for (int dy = 0; dy < 8; dy++)
        for (int dx = 0; dx < 8; dx++) {
            gint t = (cy*8+dy)*32 + (cx*8+dx);
            r += sd->avg_r[t];
            g += sd->avg_g[t];
            b += sd->avg_b[t];
        }
        sd->coarse_r[cy*4+cx] = r / 64;
        sd->coarse_g[cy*4+cx] = g / 64;
        sd->coarse_b[cy*4+cx] = b / 64;
    }
    sd->coarse_filled = TRUE;
}

void image_sim_coarse_rot_grid(const guint8 in_r[16], const guint8 in_g[16], const guint8 in_b[16],
                               guint8 out_r[16], guint8 out_g[16], guint8 out_b[16],
                               gint transfo)
{
    for (gint cy = 0; cy < 4; cy++)
    for (gint cx = 0; cx < 4; cx++)
    {
        gint si, sj;
        if (transfo & 1) { si = cx; sj = cy; }
        else             { si = cy; sj = cx; }
        if (transfo & 2) sj = 3 - sj;
        if (transfo & 4) si = 3 - si;
        gint src = si * 4 + sj;
        gint dst = cy * 4 + cx;
        out_r[dst] = in_r[src];
        out_g[dst] = in_g[src];
        out_b[dst] = in_b[src];
    }
}

void image_sim_coarse_rot(const ImageSimilarityData *sd, gint transfo,
                          guint8 out_r[16], guint8 out_g[16], guint8 out_b[16])
{
    image_sim_coarse_rot_grid(sd->coarse_r, sd->coarse_g, sd->coarse_b,
                              out_r, out_g, out_b, transfo);
}

/* coarse_perm[t][dst] = src: cell dst of the t-th orientation comes from
 * cell src of the original grid. Same mapping as image_sim_coarse_rot_grid(),
 * tabulated once by rotating an identity grid. */
static guint8 coarse_perm[8][16];
static gsize coarse_perm_init = FALSE; /* only access through g_once_* */

static void coarse_perm_init_tables(void)
{
    guint8 id[16], sink[16];
    for (gint i = 0; i < 16; i++)
        id[i] = i;

    for (gint t = 0; t < 8; t++)
        image_sim_coarse_rot_grid(id, id, id, coarse_perm[t], sink, sink, t);
}

static gint sim_vp_entry_dist(gconstpointer a, gconstpointer b)
{
    const SimVPEntry *ea = a, *eb = b;
    gint d = 0;
    for (gint i = 0; i < 16; i++)
    {
        d += abs(ea->coarse_r[i] - eb->coarse_r[i]);
        d += abs(ea->coarse_g[i] - eb->coarse_g[i]);
        d += abs(ea->coarse_b[i] - eb->coarse_b[i]);
    }
    return d;
}

static gint sim_vp_entry_dist_orb(gconstpointer a, gconstpointer b)
{
    const SimVPEntry *ea = a, *eb = b;
    gint best = G_MAXINT;
    for (gint t = 0; t < 8; t++)
    {
        const guint8 *perm = coarse_perm[t];
        gint d = 0;
        for (gint i = 0; i < 16; i++)
        {
            const gint s = perm[i];
            d += abs(ea->coarse_r[s] - eb->coarse_r[i]);
            d += abs(ea->coarse_g[s] - eb->coarse_g[i]);
            d += abs(ea->coarse_b[s] - eb->coarse_b[i]);
        }
        if (best > d)
            best = d;
    }
    return best;
}

VPTree *image_sim_vptree_build(GList *entries, gboolean rot)
{
    if (rot && g_once_init_enter(&coarse_perm_init))
    {
        coarse_perm_init_tables();
        g_once_init_leave(&coarse_perm_init, TRUE);
    }
    return vptree_build(entries, rot ? sim_vp_entry_dist_orb : sim_vp_entry_dist);
}

GList *image_sim_vptree_query(VPTree *tree, gpointer query, gint radius)
{
    return vptree_range_query(tree, query, radius);
}

/* Split n source pixels into 32 cells. For n >= 32 the cells are contiguous
 * and cover the axis; for n < 32 each cell is one sampled pixel. */
static void image_sim_cell_bounds(gint n, gint start[32], gint len[32])
{
    if (n < 32)
    {
        for (gint k = 0; k < 32; k++)
        {
            start[k] = (gint)((gfloat)n / 32 * k);
            len[k] = 1;
        }
        return;
    }

    gint pos = 0, left = n;
    for (gint k = 0; k < 32; k++)
    {
        gint inc = (gint)roundf((gfloat)left / (32 - k));
        start[k] = pos;
        len[k] = inc;
        pos += inc;
        left -= inc;
    }
}

void image_sim_fill_data(ImageSimilarityData *sd, const GdkPixbuf *pixbuf)
{
    if (!sd || !pixbuf) return;

    const gint w              = gdk_pixbuf_get_width(pixbuf);
    const gint h              = gdk_pixbuf_get_height(pixbuf);
    const gsize rs            = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar *pix         = gdk_pixbuf_read_pixels(pixbuf);
    const gboolean has_alpha  = gdk_pixbuf_get_has_alpha(pixbuf);
    const gint p_step         = has_alpha ? 4 : 3;

    gint xs_start[32], xs_len[32], ys_start[32], ys_len[32];
    image_sim_cell_bounds(w, xs_start, xs_len);
    image_sim_cell_bounds(h, ys_start, ys_len);

    for (gint ys = 0; ys < 32; ys++)
    {
        guint32 sum[32][3] = { { 0 } };
        guint32 cnt[32]    = { 0 };   /* pixels counted (opaque ones if alpha) */

        for (gint y = ys_start[ys]; y < ys_start[ys] + ys_len[ys]; y++)
        {
            const guchar *row = pix + (gsize)y * rs;

            for (gint xs = 0; xs < 32; xs++)
            {
                const guchar *p = row + (gsize)xs_start[xs] * p_step;
                const gint n = xs_len[xs];
                guint32 r = 0, g = 0, b = 0;

                if (has_alpha)
                {
                    guint32 opaque = 0;
                    for (gint x = 0; x < n; x++, p += 4)
                    {
                        guint32 visible = !!p[3];
                        r += p[0] * visible;
                        g += p[1] * visible;
                        b += p[2] * visible;
                        opaque += visible;
                    }
                    cnt[xs] += opaque;
                }
                else
                {
                    for (gint x = 0; x < n; x++, p += 3)
                    {
                        r += p[0];
                        g += p[1];
                        b += p[2];
                    }
                    cnt[xs] += n;
                }

                sum[xs][0] += r;
                sum[xs][1] += g;
                sum[xs][2] += b;
            }
        }

        for (gint xs = 0; xs < 32; xs++)
        {
            guint32 c = cnt[xs] ? cnt[xs] : 1;
            gint t = ys * 32 + xs;
            sd->avg_r[t] = sum[xs][0] / c;
            sd->avg_g[t] = sum[xs][1] / c;
            sd->avg_b[t] = sum[xs][2] / c;
        }
    }

    sd->filled = TRUE;
}

ImageSimilarityData *image_sim_new_from_pixbuf(const GdkPixbuf *pixbuf)
{
    ImageSimilarityData *sd = image_sim_new();
    image_sim_fill_data(sd, pixbuf);

    return sd;
}

gdouble image_sim_compare_transfo(ImageSimilarityData *a, ImageSimilarityData *b, gchar transfo)
{
    if (!a || !b || !a->filled || !b->filled) return 0.0;

    gint i2, *i;
    gint j2, *j;
    gint sim = 0;

    if (transfo & 1) { i = &j2; j = &i2; } else { i = &i2; j = &j2; }
    for (gint j1 = 0; j1 < 32; j1++)
    {
        if (transfo & 2) *j = 31-j1; else *j = j1;
        for (gint i1 = 0; i1 < 32; i1++)
        {
            if (transfo & 4) *i = 31-i1; else *i = i1;
            sim += abs(a->avg_r[i1*32+j1] - b->avg_r[i2*32+j2]);
            sim += abs(a->avg_g[i1*32+j1] - b->avg_g[i2*32+j2]);
            sim += abs(a->avg_b[i1*32+j1] - b->avg_b[i2*32+j2]);
        }
    }

    return 1.0 - ((gdouble)sim / (255.0 * 1024.0 * 3.0));
}

gdouble image_sim_compare(ImageSimilarityData *a, ImageSimilarityData *b, gboolean rot)
{
    gint max_t = rot ? 8 : 1;

    gdouble score, max_score = 0;

    for (gint t = 0; t < max_t; t++)
    {
        score = image_sim_compare_transfo(a, b, t);
        if (score > max_score)
            max_score = score;
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
    {                                                                      \
        for (j1 = 0; j1 < 32; j1++)                                        \
        {                                                                  \
            sim += abs(a->avg_r[i1*32+j1] - b->avg_r[BIDX]);               \
            sim += abs(a->avg_g[i1*32+j1] - b->avg_g[BIDX]);               \
            sim += abs(a->avg_b[i1*32+j1] - b->avg_b[BIDX]);               \
        }                                                                  \
        if (sim > max_sim) return 0.0;                                     \
    }                                                                      \
    return 1.0 - ((gdouble)sim / (255.0 * 1024.0 * 3.0));                  \
}

/* Transpose: j1 outer, i1 inner — b sequential, a stride-32 */
#define DEFINE_TRANSFO_T(N, BIDX)                                          \
static gdouble image_sim_compare_fast_##N(                                 \
    ImageSimilarityData *a, ImageSimilarityData *b, gint max_sim)          \
{                                                                          \
    gint sim = 0, i1, j1;                                                  \
    for (j1 = 0; j1 < 32; j1++)                                            \
    {                                                                      \
        for (i1 = 0; i1 < 32; i1++)                                        \
        {                                                                  \
            sim += abs(a->avg_r[i1*32+j1] - b->avg_r[BIDX]);               \
            sim += abs(a->avg_g[i1*32+j1] - b->avg_g[BIDX]);               \
            sim += abs(a->avg_b[i1*32+j1] - b->avg_b[BIDX]);               \
        }                                                                  \
        if (sim > max_sim) return 0.0;                                     \
    }                                                                      \
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
 * a point that can simply no longer make the cut-off point. */

typedef gdouble (*transfo_fn)(ImageSimilarityData *, ImageSimilarityData *, gint);
static const transfo_fn transfo_funcs[8] = {
    image_sim_compare_fast_0, image_sim_compare_fast_6,
    image_sim_compare_fast_2, image_sim_compare_fast_4,
    image_sim_compare_fast_1, image_sim_compare_fast_3,
    image_sim_compare_fast_5, image_sim_compare_fast_7,
};

gdouble image_sim_compare_fast(ImageSimilarityData *a, ImageSimilarityData *b,
                               gdouble min, gboolean rot)
{
    if (!a || !b || !a->filled || !b->filled) return 0.0;

    gint max_t = rot ? 8 : 1;
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
