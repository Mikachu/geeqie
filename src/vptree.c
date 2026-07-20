#include "main.h"
#include "vptree.h"

typedef struct _VPNode VPNode;
struct _VPNode
{
    gpointer  item;
    gint      mu;      /* partition radius (median distance to children) */
    VPNode   *left;    /* items with dist(vp, x) <= mu */
    VPNode   *right;   /* items with dist(vp, x) > mu */
};

struct _VPTree
{
    VPNode        *root;
    VPTreeDistFunc dist;
};

/* --- build --- */

typedef struct {
    gpointer item;
    gint     dist;
} DistItem;

static gint dist_item_cmp(gconstpointer a, gconstpointer b)
{
    const DistItem *da = a;
    const DistItem *db = b;
    return da->dist - db->dist;
}

static VPNode *build_node(GArray *items, VPTreeDistFunc dist)
{
    if (items->len == 0) return NULL;

    VPNode *node = g_new0(VPNode, 1);
    node->item = g_array_index(items, DistItem, 0).item;

    if (items->len == 1) return node;

    /* compute distances from vantage point to all other items */
    GArray *rest = g_array_new(FALSE, FALSE, sizeof(DistItem));
    for (guint i = 1; i < items->len; i++)
    {
        DistItem di;
        di.item = g_array_index(items, DistItem, i).item;
        di.dist = dist(node->item, di.item);
        g_array_append_val(rest, di);
    }

    /* sort by distance to find median */
    g_array_sort(rest, dist_item_cmp);
    guint mid = rest->len / 2;
    node->mu = g_array_index(rest, DistItem, mid).dist;

    /* partition into left (dist <= mu) and right (dist > mu) */
    GArray *left  = g_array_new(FALSE, FALSE, sizeof(DistItem));
    GArray *right = g_array_new(FALSE, FALSE, sizeof(DistItem));
    for (guint i = 0; i < rest->len; i++)
    {
        DistItem *di = &g_array_index(rest, DistItem, i);
        if (di->dist <= node->mu)
            g_array_append_val(left, *di);
        else
            g_array_append_val(right, *di);
    }
    g_array_free(rest, TRUE);

    node->left  = build_node(left,  dist);
    node->right = build_node(right, dist);
    g_array_free(left,  TRUE);
    g_array_free(right, TRUE);

    return node;
}

VPTree *vptree_build(GList *items, VPTreeDistFunc dist)
{
    VPTree *tree = g_new0(VPTree, 1);
    tree->dist = dist;

    GArray *arr = g_array_new(FALSE, FALSE, sizeof(DistItem));
    for (GList *work = items; work; work = work->next)
    {
        DistItem di = { .item = work->data, .dist = 0 };
        g_array_append_val(arr, di);
    }

    tree->root = build_node(arr, dist);
    g_array_free(arr, TRUE);
    return tree;
}

/* --- free --- */

static void free_node(VPNode *node)
{
    if (!node) return;
    free_node(node->left);
    free_node(node->right);
    g_free(node);
}

void vptree_free(VPTree *tree)
{
    if (!tree) return;
    free_node(tree->root);
    g_free(tree);
}

/* --- range query --- */

static void query_node(VPNode *node, gconstpointer query, gint radius,
                       VPTreeDistFunc dist, GList **results)
{
    if (!node) return;

    gint d = dist(query, node->item);

    if (d <= radius)
        *results = g_list_prepend(*results, node->item);

    /* left subtree: items with dist(vp, x) <= mu; reachable if d <= mu + radius */
    if (node->left && d <= node->mu + radius)
        query_node(node->left,  query, radius, dist, results);

    /* right subtree: items with dist(vp, x) > mu; reachable if d + radius >= mu */
    if (node->right && d + radius >= node->mu)
        query_node(node->right, query, radius, dist, results);
}

GList *vptree_range_query(VPTree *tree, gconstpointer query, gint radius)
{
    GList *results = NULL;
    if (tree)
        query_node(tree->root, query, radius, tree->dist, &results);
    return results;
}
