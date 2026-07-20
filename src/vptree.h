#ifndef VPTREE_H
#define VPTREE_H

#include "main.h"

typedef gint (*VPTreeDistFunc)(gconstpointer a, gconstpointer b);

typedef struct _VPTree VPTree;

/* Build a VP-tree from a GList of items.
 * dist_func: L1 distance between two items (must be a metric).
 */
VPTree *vptree_build(GList *items, VPTreeDistFunc dist_func);
void    vptree_free(VPTree *tree);

/* Find all items within distance <= radius of query.
 * Returns a GList of gpointer (not owned). Caller must g_list_free(). */
GList  *vptree_range_query(VPTree *tree, gconstpointer query, gint radius);

#endif
