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

#ifndef DUPE_H
#define DUPE_H

#include "similar.h"

/* match methods */
typedef enum
{
    DUPE_MATCH_NONE = 0,
    DUPE_MATCH_NAME = 1 << 0,
    DUPE_MATCH_SIZE = 1 << 1,
    DUPE_MATCH_DATE = 1 << 2,
    DUPE_MATCH_DIM  = 1 << 3,   /* image dimensions */
    DUPE_MATCH_SUM  = 1 << 4,   /* MD5sum */
    DUPE_MATCH_PATH = 1 << 5,
    DUPE_MATCH_SIM_HIGH = 1 << 6,   /* similarity */
    DUPE_MATCH_SIM_MED  = 1 << 7,
    DUPE_MATCH_SIM_LOW  = 1 << 8,
    DUPE_MATCH_SIM_CUSTOM = 1 << 9,
    DUPE_MATCH_NAME_CI = 1 << 10    /* same as name, but case insensitive */
} DupeMatchType;

typedef struct _DupeItem DupeItem;
struct _DupeItem
{
    CollectionData *collection; /* NULL if from DupeWindow->files */
    CollectInfo *info;

    FileData *fd;

    gchar *md5sum;
    gint width;
    gint height;

    ImageSimilarityData *simd;

    /* thumb */
    GdkPixbuf *pixbuf;

    GList *group;           /* List of match data */
    gdouble group_rank;

    gint second;
};

typedef struct _DupeMatch DupeMatch;
struct _DupeMatch
{
    DupeItem *di;
    gdouble rank;
};

typedef struct _DupeWindow DupeWindow;
struct _DupeWindow
{
    GList *list;            /* dropped files (DupeItem) */
    GList *dupes;           /* list of dupes (DupeItem, grouping the DupeMatches) */
    DupeMatchType match_mask;   /* mask of things to check for match */

    GtkWidget *window;
    GtkWidget *table;
    GtkWidget *listview;
    GtkWidget *combo;
    GtkWidget *status_label;
    GtkWidget *extra_label;
    GtkWidget *button_thumbs;
    GList *add_files_queue;
    guint add_files_queue_id;
    GHashTable *list_cache;
    GHashTable *second_list_cache;
    GtkWidget *controls_box;

    gboolean show_thumbs;

    guint idle_id; /* event source id */
    GList *working;
    gint setup_done;
    gint setup_count;
    gint setup_n;           /* these are merely for speed optimization */
    GList *setup_point;     /* ditto */
    DupeMatchType setup_mask;   /* ditto */
    guint64 setup_time;
    guint64 setup_time_count;

    DupeItem *click_item;       /* for popup menu */

    ThumbLoader *thumb_loader;
    DupeItem *thumb_item;

    ImageLoader *img_loader;

    /* second set comparison stuff */

    gboolean second_set;        /* second set enabled ? */
    GList *second_list;     /* second set dropped files */
    gboolean second_drop;       /* drop is on second set */

    GtkWidget *second_vbox;     /* box of second widgets */
    GtkWidget *second_listview;
    GtkWidget *second_status_label;

    gboolean color_frozen;
};


DupeWindow *dupe_window_new(DupeMatchType match_mask);

void dupe_window_clear(DupeWindow *dw);
void dupe_window_close(DupeWindow *dw);

void dupe_window_add_collection(DupeWindow *dw, CollectionData *collection);
void dupe_window_add_files(DupeWindow *dw, GList *list, gboolean recurse);

/* cell max with/height hack utility */
void cell_renderer_height_override(GtkCellRenderer *renderer);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
