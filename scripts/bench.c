#/*
    gcc -o bench bench.c src/similar.c src/vptree.c $(pkg-config --cflags --libs gtk+-2.0) -fcommon $CFLAGS
    exit $!
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "src/main.h"
#include "src/similar.h"

ImageSimilarityData *load_sim(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    char b = 0;
    while (b != '=' && fread(&b, 1, 1, f) == 1)
      ;
    if (b != '=') {
        fprintf(stderr, "invalid file %s\n", path);
        fclose(f);
        return NULL;
    }
    ImageSimilarityData *sd = image_sim_new();
    guint8 buf[3];
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            if (fread(buf, 3, 1, f) < 1) {
                fprintf(stderr, "partial file %s\n", path);
                fclose(f);
                image_sim_free(sd);
                return NULL;
            }
            sd->avg_r[y*32+x] = buf[0];
            sd->avg_g[y*32+x] = buf[1];
            sd->avg_b[y*32+x] = buf[2];
        }
    }
    sd->filled = TRUE;
    fclose(f);
    return sd;
}

int main(int argc, char *argv[]) {
    static ConfOptions bench_options = { .rot_invariant_sim = TRUE, };
    options = &bench_options;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <threshold 0.0-1.0> <file.sim> ...\n", argv[0]);
        return 1;
    }

    gdouble threshold = atof(argv[1]);
    int n = argc - 2;
    ImageSimilarityData **sims = malloc(n * sizeof(*sims));
    for (int i = 0, j = 2; i < n; i++, j++) {
        sims[i] = load_sim(argv[j]);
        if (sims[i] == NULL) {
            i--; n--;
        }
    }

    fprintf(stderr, "loaded %d files, running %ld pairs at threshold %.2f\n",
            n, (long)n * (n - 1) / 2, threshold);

    struct timespec t0, t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < n; i++)
        image_sim_calc_coarse(sims[i]);

    gint max_t = bench_options.rot_invariant_sim ? 8 : 1;
    GList *entries = NULL;
    SimVPEntry *entry_buf = malloc(n * max_t * sizeof(SimVPEntry));
    for (int i = 0; i < n; i++)
    {
        for (int t = 0; t < max_t; t++)
        {
            SimVPEntry *e = &entry_buf[i * max_t + t];
            image_sim_coarse_rot(sims[i], t, e->coarse_r, e->coarse_g, e->coarse_b);
            e->user_data = sims[i];
            entries = g_list_prepend(entries, e);
        }
    }
    VPTree *vptree = image_sim_vptree_build(entries);
    g_list_free(entries);

    GHashTable *sim_idx = g_hash_table_new(NULL, NULL);
    for (int i = 0; i < n; i++)
        g_hash_table_insert(sim_idx, sims[i], GINT_TO_POINTER(i));

    gint radius = (gint)((1.0 - threshold) * 255.0 * 16.0 * 3.0);
    long total_pairs = (long)n * (n - 1) / 2;
    long vp_candidates = 0;
    long matches = 0;
    gdouble checksum = 0.0;

    for (int i = 0; i < n; i++)
    {
        SimVPEntry query;
        image_sim_coarse_rot(sims[i], 0, query.coarse_r, query.coarse_g, query.coarse_b);
        query.user_data = sims[i];
        GList *candidates = image_sim_vptree_query(vptree, &query, radius);
        GHashTable *seen = g_hash_table_new(NULL, NULL);
        for (GList *c = candidates; c; c = c->next)
        {
            SimVPEntry *e = c->data;
            ImageSimilarityData *other = e->user_data;
            if (other == sims[i]) continue;
            gint j = GPOINTER_TO_INT(g_hash_table_lookup(sim_idx, other));
            if (j <= i) continue;
            if (g_hash_table_contains(seen, other)) continue;
            g_hash_table_add(seen, other);
            vp_candidates++;
            gdouble s = image_sim_compare_fast(sims[i], other, threshold);
            checksum += s;
            if (s > 0.0) matches++;
        }
        g_hash_table_destroy(seen);
        g_list_free(candidates);
    }

    g_hash_table_destroy(sim_idx);
    vptree_free(vptree);
    free(entry_buf);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    printf("pairs: %ld  vp_candidates: %ld (%.1f%%)  vp_skipped: %ld (%.1f%%)  "
           "elapsed: %.3f  checksum: %.6f\n",
           total_pairs, vp_candidates,
           100.0 * vp_candidates / total_pairs,
           total_pairs - vp_candidates,
           100.0 * (total_pairs - vp_candidates) / total_pairs, elapsed, checksum);

    long pairs = 0;
    matches = 0;
    checksum = 0.0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            gdouble s = image_sim_compare_fast(sims[i], sims[j], threshold);
            checksum += s;
            if (s > 0.0) matches++;
            pairs++;
        }

    clock_gettime(CLOCK_MONOTONIC, &t2);
    elapsed = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) * 1e-9;

    printf("pairs: %ld  time: %.3fs  throughput: %.0f pairs/s  checksum: %.6f\n",
           pairs, elapsed, (double)pairs / elapsed, checksum);
    printf("matches: %ld (%.2f%%)\n", matches, 100.0 * matches / pairs);

    free(sims);
    return 0;
}
