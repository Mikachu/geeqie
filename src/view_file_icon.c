/*
 * Copyright (C) 2006 John Ellis
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

#include "main.h"
#include "view_file_icon.h"

#include "bar.h"
#include "cellrenderericon.h"
#include "collect.h"
#include "collect-io.h"
#include "collect-table.h"
#include "editors.h"
#include "filedata.h"
#include "layout.h"
#include "layout_image.h"
#include "menu.h"
#include "thumb.h"
#include "utilops.h"
#include "ui_fileops.h"
#include "ui_menu.h"
#include "ui_tree_edit.h"
#include "view_file.h"

#include <gdk/gdkkeysyms.h> /* for keyboard values */


/* between these, the icon width is increased by thumb_max_width / 2 */
#define THUMB_MIN_ICON_WIDTH 128
#define THUMB_MAX_ICON_WIDTH 150

#define VFICON_MAX_COLUMNS 32
#define THUMB_BORDER_PADDING 2

#define VFICON_TIP_DELAY 500

enum {
    FILE_COLUMN_POINTER = 0,
    FILE_COLUMN_COUNT
};

typedef enum {
    SELECTION_NONE      = 0,
    SELECTION_SELECTED  = 1 << 0,
    SELECTION_PRELIGHT  = 1 << 1,
    SELECTION_FOCUS     = 1 << 2
} SelectionType;

typedef struct IconData IconData;
struct IconData
{
    SelectionType selected;
    FileData *fd;
};

static IconData *vficon_icon_data(ViewFile *vf, FileData *fd)
{
    if (!fd) return NULL;

    for (GList *work = vf->list; work; work = work->next)
    {
        IconData *chk = work->data;
        if (chk->fd == fd) return chk;
    }
    return NULL;
}

static void iconlist_free_item(gpointer data)
{
    IconData *id = data;
    file_data_unref(id->fd);
    g_free(id);
}

gint iconlist_sort_file_cb(gconstpointer a, gconstpointer b, gpointer data)
{
    const IconData *ida = a,
                   *idb = b;
    return filelist_sort_compare_filedata_cb(ida->fd, idb->fd, data);
}

GList *iconlist_sort(GList *list, SortType method, gboolean ascend)
{
    return filelist_sort_full(list, method, ascend, iconlist_sort_file_cb);
}

GList *iconlist_insert_sort(GList *list, IconData *id, SortType method, gboolean ascend)
{
    return filelist_insert_sort_full(list, id, method, ascend, iconlist_sort_file_cb);
}


static void vficon_toggle_filenames(ViewFile *vf);
static void vficon_selection_remove(ViewFile *vf, IconData *id, SelectionType mask, GtkTreeIter *iter);
static void vficon_move_focus(ViewFile *vf, gint row, gint col, gboolean relative);
static void vficon_set_focus(ViewFile *vf, IconData *id);
static void vficon_populate_at_new_size(ViewFile *vf, gint w, gint h, gboolean force);


/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */

FileData *vficon_clicked_fd(ViewFile *vf)
{
    return (VFICON(vf)->click_id) ? VFICON(vf)->click_id->fd : NULL;
}

gboolean vficon_fd_selected(ViewFile *vf, FileData *fd)
{
    IconData *id = vficon_icon_data(vf, fd);

    return (id && (id->selected & SELECTION_SELECTED));
}

GList *vficon_selection_get_one(ViewFile *vf, FileData *fd)
{
    return g_list_prepend(filelist_copy(fd->sidecar_files), file_data_ref(fd));
}

void vficon_clicked_clear(ViewFile *vf)
{
    vficon_selection_remove(vf, VFICON(vf)->click_id, SELECTION_PRELIGHT, NULL);
    VFICON(vf)->click_id = NULL;
}

void vficon_pop_menu_rename_cb(GtkWidget *widget, ViewFile *vf)
{
    file_util_rename(NULL, vf_pop_menu_file_list(vf), vf->listview);
}

void vficon_pop_menu_show_names_cb(GtkWidget *widget, ViewFile *vf)
{
    vficon_toggle_filenames(vf);
}

/*
 *-------------------------------------------------------------------
 * signals
 *-------------------------------------------------------------------
 */

static void vficon_toggle_filenames(ViewFile *vf)
{
    GtkAllocation allocation;
    VFICON(vf)->show_text = !VFICON(vf)->show_text;
    options->show_icon_names = VFICON(vf)->show_text;

    gtk_widget_get_allocation(vf->listview, &allocation);
    vficon_populate_at_new_size(vf, allocation.width, allocation.height, TRUE);
}

static gint vficon_get_icon_width(ViewFile *vf)
{
    gint width;

    if (!VFICON(vf)->show_text) return options->thumbnails.max_width;

    width = options->thumbnails.max_width + options->thumbnails.max_width / 2;
    if (width < THUMB_MIN_ICON_WIDTH) width = THUMB_MIN_ICON_WIDTH;
    if (width > THUMB_MAX_ICON_WIDTH) width = options->thumbnails.max_width;

    return width;
}

/*
 *-------------------------------------------------------------------
 * misc utils
 *-------------------------------------------------------------------
 */

static gboolean vficon_find_position(ViewFile *vf, IconData *id, gint *row, gint *col)
{
    gint n = g_list_index(vf->list, id);

    if (n < 0) return FALSE;

    *row = n / VFICON(vf)->columns;
    *col = n - (*row * VFICON(vf)->columns);

    return TRUE;
}

static gboolean vficon_find_iter(ViewFile *vf, IconData *id, GtkTreeIter *iter, gint *column)
{
    gint row, col;

    GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

    if (!vficon_find_position(vf, id, &row, &col)) return FALSE;
    if (!gtk_tree_model_iter_nth_child(store, iter, NULL, row)) return FALSE;
    if (column) *column = col;

    return TRUE;
}

static IconData *vficon_find_data(ViewFile *vf, gint row, gint col, GtkTreeIter *iter)
{
    if (row >= 0 && col >= 0)
    {
        GtkTreeIter p;
        GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
        if (gtk_tree_model_iter_nth_child(store, &p, NULL, row))
        {
            GList *list;

            gtk_tree_model_get(store, &p, FILE_COLUMN_POINTER, &list, -1);
            if (!list) return NULL;

            if (iter) *iter = p;

            return g_list_nth_data(list, col);
        }
    }
    return NULL;
}

static IconData *vficon_find_data_by_coord(ViewFile *vf, gint x, gint y, GtkTreeIter *iter)
{
    GtkTreePath *tpath;
    GtkTreeViewColumn *column;

    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), x, y,
                                      &tpath, &column, NULL, NULL))
    {
        GtkTreeIter row;
        GList *list;

        GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
        gtk_tree_model_get_iter(store, &row, tpath);
        gtk_tree_path_free(tpath);

        gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &list, -1);

        gint n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "column_number"));
        if (list)
        {
            if (iter) *iter = row;
            return g_list_nth_data(list, n);
        }
    }
    return NULL;
}

static void vficon_mark_toggled_cb(GtkCellRendererToggle *cell, gchar *path_str, ViewFile *vf)
{
    GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
    GtkTreeIter row;
    gint column;
    GList *list;
    guint toggled_mark;
    IconData *id;

    if (!path)
        return;

    GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
    if (!gtk_tree_model_get_iter(store, &row, path))
    {
        gtk_tree_path_free(path);
        return;
    }
    gtk_tree_path_free(path);

    gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &list, -1);

    column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), "column_number"));
    g_object_get(G_OBJECT(cell), "toggled_mark", &toggled_mark, NULL);

    id = g_list_nth_data(list, column);
    if (id)
    {
        FileData *fd = id->fd;
        file_data_set_mark(fd, toggled_mark, !file_data_get_mark(fd, toggled_mark));
    }
}


/*
 *-------------------------------------------------------------------
 * tooltip type window
 *-------------------------------------------------------------------
 */

static void tip_show(ViewFile *vf)
{
    GtkWidget *label;
    ViewFileInfoIcon *vfi = VFICON(vf);

    if (vfi->tip_window) return;

    vfi->tip_id = vficon_find_data_by_coord(vf, vfi->x, vfi->y, NULL);
    if (!vfi->tip_id) return;

    vfi->tip_window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_resizable(GTK_WINDOW(vfi->tip_window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(vfi->tip_window), 2);

    label = gtk_label_new(vfi->tip_id->fd->name);

    g_object_set_data(G_OBJECT(vfi->tip_window), "tip_label", label);
    gtk_container_add(GTK_CONTAINER(vfi->tip_window), label);
    gtk_widget_show(label);

    if (!gtk_widget_get_realized(vfi->tip_window))
        gtk_widget_realize(vfi->tip_window);

    gtk_window_move(GTK_WINDOW(vfi->tip_window), vfi->x_root + 16, vfi->y_root + 16);
    gtk_widget_show(vfi->tip_window);
}

static void tip_hide(ViewFile *vf)
{
    g_clear_pointer(&VFICON(vf)->tip_window, gtk_widget_destroy);
}

static gboolean tip_schedule_cb(gpointer data)
{
    ViewFile *vf = data;
    GtkWidget *window;

    if (!VFICON(vf)->tip_delay_id) return FALSE;

    window = gtk_widget_get_toplevel(vf->listview);

    if (gtk_widget_get_sensitive(window) &&
        gtk_window_has_toplevel_focus(GTK_WINDOW(window)))
    {
        tip_show(vf);
    }

    VFICON(vf)->tip_delay_id = 0;
    return FALSE;
}

static void tip_schedule(ViewFile *vf, gint x, gint y, gint x_root, gint y_root)
{
    tip_hide(vf);

    if (VFICON(vf)->tip_delay_id)
    {
        if (abs(x - VFICON(vf)->x) + abs(y - VFICON(vf)->y) > 4)
            g_clear_handle_id(&VFICON(vf)->tip_delay_id, g_source_remove);
        else
            return;
    }

    VFICON(vf)->x = x;
    VFICON(vf)->y = y;
    VFICON(vf)->x_root = x_root;
    VFICON(vf)->y_root = y_root;

    if (!VFICON(vf)->show_text)
        VFICON(vf)->tip_delay_id = g_timeout_add(VFICON_TIP_DELAY, tip_schedule_cb, vf);
}

static void tip_unschedule(ViewFile *vf)
{
    tip_hide(vf);

    g_clear_handle_id(&VFICON(vf)->tip_delay_id, g_source_remove);
}

static void tip_update(ViewFile *vf, gint x, gint y, gint x_root, gint y_root)
{
    ViewFileInfoIcon *vfi = VFICON(vf);

    if (vfi->tip_window)
    {
        IconData *id = vficon_find_data_by_coord(vf, x, y, NULL);
        if (id != vfi->tip_id)
        {
            vfi->tip_id = id;
            gtk_window_move(GTK_WINDOW(vfi->tip_window), x_root + 16, y_root + 16);

            if (!vfi->tip_id)
            {
                tip_hide(vf);
                tip_schedule(vf, x, y, x_root, y_root);
                return;
            }

            GtkWidget *label = g_object_get_data(G_OBJECT(vfi->tip_window), "tip_label");
            gtk_label_set_text(GTK_LABEL(label), vfi->tip_id->fd->name);
        }
    }
    else
    {
        tip_schedule(vf, x, y, x_root, y_root);
    }
}


/*
 *-------------------------------------------------------------------
 * cell updates
 *-------------------------------------------------------------------
 */

static void vficon_selection_set(ViewFile *vf, IconData *id, SelectionType value, GtkTreeIter *iter)
{
    GList *list;

    if (!id) return;

    if (id->selected == value) return;
    id->selected = value;

    GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
    if (iter)
    {
        gtk_tree_model_get(store, iter, FILE_COLUMN_POINTER, &list, -1);
        if (list)
            gtk_list_store_set(GTK_LIST_STORE(store), iter, FILE_COLUMN_POINTER, list, -1);
    }
    else
    {
        GtkTreeIter row;

        if (vficon_find_iter(vf, id, &row, NULL))
        {
            gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &list, -1);
            if (list)
                gtk_list_store_set(GTK_LIST_STORE(store), &row, FILE_COLUMN_POINTER, list, -1);
        }
    }
}

static void vficon_selection_add(ViewFile *vf, IconData *id, SelectionType mask, GtkTreeIter *iter)
{
    if (id)
        vficon_selection_set(vf, id, id->selected | mask, iter);
}

static void vficon_selection_remove(ViewFile *vf, IconData *id, SelectionType mask, GtkTreeIter *iter)
{
    if (id)
        vficon_selection_set(vf, id, id->selected & ~mask, iter);
}

void vficon_marks_set(ViewFile *vf, gint enable)
{
    GtkAllocation allocation;
    gtk_widget_get_allocation(vf->listview, &allocation);
    vficon_populate_at_new_size(vf, allocation.width, allocation.height, TRUE);
}

/*
 *-------------------------------------------------------------------
 * selections
 *-------------------------------------------------------------------
 */

static gint vficon_index_by_id(ViewFile *vf, IconData *in_id)
{
    gint p = 0;

    if (!in_id) return -1;

    for (GList *work = vf->list; work; work = work->next, p++)
        if (work->data == in_id)
            return p;

    return -1;
}

static void vficon_verify_selections(ViewFile *vf)
{
    ViewFileInfoIcon *vfi = VFICON(vf);

    for (GList *work = vfi->selection, *next; work; work = next)
    {
        IconData *id = work->data;
        next = work->next;

        if (vficon_index_by_id(vf, id) >= 0) continue;

        vfi->selection = g_list_delete_link(vfi->selection, work);
    }
}

void vficon_select_all(ViewFile *vf)
{
    g_clear_list(&VFICON(vf)->selection, NULL);

    for (GList *work = vf->list; work; work = work->next)
    {
        IconData *id = work->data;

        VFICON(vf)->selection = g_list_prepend(VFICON(vf)->selection, id);
        vficon_selection_add(vf, id, SELECTION_SELECTED, NULL);
    }

    vf_send_update(vf);
}

void vficon_select_none(ViewFile *vf)
{
    for (GList *work = VFICON(vf)->selection; work; work = work->next)
    {
        IconData *id = work->data;

        vficon_selection_remove(vf, id, SELECTION_SELECTED, NULL);
    }
    g_clear_list(&VFICON(vf)->selection, NULL);

    vf_send_update(vf);
}

void vficon_select_invert(ViewFile *vf)
{
    for (GList *work = vf->list, *next; work; work = next)
    {
        IconData *id = work->data;
        next = work->next;

        if (id->selected & SELECTION_SELECTED)
        {
            VFICON(vf)->selection = g_list_remove(VFICON(vf)->selection, id);
            vficon_selection_remove(vf, id, SELECTION_SELECTED, NULL);
        }
        else
        {
            VFICON(vf)->selection = g_list_prepend(VFICON(vf)->selection, id);
            vficon_selection_add(vf, id, SELECTION_SELECTED, NULL);
        }
    }
    vf_send_update(vf);
}

static void vficon_select(ViewFile *vf, IconData *id)
{
    VFICON(vf)->prev_selection = id;

    if (!id || id->selected & SELECTION_SELECTED) return;

    VFICON(vf)->selection = g_list_prepend(VFICON(vf)->selection, id);
    vficon_selection_add(vf, id, SELECTION_SELECTED, NULL);

    vf_send_update(vf);
}

static void vficon_unselect(ViewFile *vf, IconData *id)
{
    VFICON(vf)->prev_selection = id;

    if (!id || !(id->selected & SELECTION_SELECTED) ) return;

    VFICON(vf)->selection = g_list_remove(VFICON(vf)->selection, id);
    vficon_selection_remove(vf, id, SELECTION_SELECTED, NULL);

    vf_send_update(vf);
}

static void vficon_select_util(ViewFile *vf, IconData *id, gboolean select)
{
    if (select)
        vficon_select(vf, id);
    else
        vficon_unselect(vf, id);
}

static void vficon_select_region_util(ViewFile *vf, IconData *start, IconData *end, gboolean select)
{
    gint row1, col1;
    gint row2, col2;
    gint t;
    gint i, j;

    if (!vficon_find_position(vf, start, &row1, &col1) ||
        !vficon_find_position(vf, end,   &row2, &col2) ) return;

    VFICON(vf)->prev_selection = end;

    if (!options->collections.rectangular_selection)
    {
        if (g_list_index(vf->list, start) > g_list_index(vf->list, end))
        {
            IconData *id = start;
            start = end;
            end = id;
        }

        for (GList *work = g_list_find(vf->list, start); work; work = work->next)
        {
            IconData *id = work->data;
            vficon_select_util(vf, id, select);

            if (work->data == end)
                break;
        }
        return;
    }

    if (row2 < row1)
    {
        t = row1;
        row1 = row2;
        row2 = t;
    }
    if (col2 < col1)
    {
        t = col1;
        col1 = col2;
        col2 = t;
    }

    DEBUG_1("table: %d x %d to %d x %d", row1, col1, row2, col2);

    for (i = row1; i <= row2; i++)
    for (j = col1; j <= col2; j++)
    {
        IconData *id = vficon_find_data(vf, i, j, NULL);
        if (id) vficon_select_util(vf, id, select);
    }
}

gboolean vficon_index_is_selected(ViewFile *vf, gint row)
{
    IconData *id = g_list_nth_data(vf->list, row);

    if (!id) return FALSE;

    return (id->selected & SELECTION_SELECTED);
}

guint vficon_selection_count(ViewFile *vf, gint64 *bytes)
{
    if (bytes)
    {
        gint64 b = 0;

        for (GList *work = VFICON(vf)->selection; work; work = work->next)
        {
            IconData *id = work->data;
            FileData *fd = id->fd;
            g_assert(fd->magick == FD_MAGICK);
            b += fd->size;
        }
        *bytes = b;
    }
    return g_list_length(VFICON(vf)->selection);
}

GList *vficon_selection_get_list(ViewFile *vf)
{
    GList *list = NULL;

    for (GList *work = g_list_last(VFICON(vf)->selection); work; work = work->prev)
    {
        IconData *id = work->data;
        FileData *fd = id->fd;
        g_assert(fd->magick == FD_MAGICK);

        list = g_list_concat(filelist_copy(fd->sidecar_files), list);
        list = g_list_prepend(list, file_data_ref(fd));
    }

    return list;
}

GList *vficon_selection_get_list_by_index(ViewFile *vf)
{
    GList *list = NULL;

    for (GList *work = VFICON(vf)->selection; work; work = work->next)
        list = g_list_prepend(list, GINT_TO_POINTER(g_list_index(vf->list, work->data)));

    return g_list_reverse(list);
}

static void vficon_select_by_id(ViewFile *vf, IconData *id)
{
    if (!id) return;

    if (!(id->selected & SELECTION_SELECTED))
    {
        vf_select_none(vf);
        vficon_select(vf, id);
    }

    vficon_set_focus(vf, id);
}

void vficon_select_by_fd(ViewFile *vf, FileData *fd)
{
    if (!fd) return;

    for (GList *work = vf->list; work; work = work->next)
    {
        IconData *chk = work->data;
        if (chk->fd == fd)
            return vficon_select_by_id(vf, chk);
    }
    vficon_select_by_id(vf, NULL);
}

static void vficon_fd_mark_updated(ViewFile *vf, FileData *fd)
{
    GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
    GtkTreeIter row;
    GList *list;

    if (vficon_find_iter(vf, vficon_icon_data(vf, fd), &row, NULL))
    {
        gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &list, -1);
        if (list)
            gtk_list_store_set(GTK_LIST_STORE(store),
                               &row, FILE_COLUMN_POINTER, list, -1);
    }
}

void vficon_mark_to_selection(ViewFile *vf, gint mark, MarkToSelectionMode mode)
{
    gint n = mark - 1;

    g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

    for (GList *work = vf->list; work; work = work->next)
    {
        IconData *id = work->data;
        FileData *fd = id->fd;
        gboolean mark_val, selected;

        g_assert(fd->magick == FD_MAGICK);

        mark_val = file_data_get_mark(fd, n);
        selected = (id->selected & SELECTION_SELECTED);

        selected = vf_mts_select(fd, n, selected, mode);

        vficon_select_util(vf, id, selected);
    }
}

/*
 *-------------------------------------------------------------------
 * focus
 *-------------------------------------------------------------------
 */

static void vficon_move_focus(ViewFile *vf, gint row, gint col, gboolean relative)
{
    ViewFileInfoIcon *vfi = VFICON(vf);
    gint index;

    if (vfi->rows <= 0 || vfi->columns <= 0)
    {
        vficon_set_focus(vf, NULL);
        return;
    }

    if (relative)
    {
        /* clamp the vertical move first, then apply the horizontal
           delta in linear space */
        gint new_row = CLAMP(vfi->focus_row + row, 0, vfi->rows - 1);
        index = new_row * vfi->columns + vfi->focus_column + col;
    }
    else
    {
        index = CLAMP(row, 0, vfi->rows - 1) * vfi->columns +
                CLAMP(col, 0, vfi->columns - 1);
    }

    index = CLAMP(index, 0, vfi->rows * vfi->columns - 1);

    if (index / vfi->columns == vfi->rows - 1)
    {
        gint count = g_list_length(vf->list);
        index = CLAMP(index, (vfi->rows - 1) * vfi->columns, count - 1);
    }

    vficon_set_focus(vf, vficon_find_data(vf,
                                          index / vfi->columns,
                                          index % vfi->columns,
                                          NULL));
}

static void vficon_set_focus(ViewFile *vf, IconData *id)
{
    GtkTreeIter iter;
    gint row, col;
    ViewFileInfoIcon *vfi = VFICON(vf);

    if (g_list_find(vf->list, vfi->focus_id))
    {
        if (id == vfi->focus_id)
        {
            /* ensure focus row col are correct */
            vficon_find_position(vf, vfi->focus_id, &vfi->focus_row, &vfi->focus_column);
            return;
        }
        vficon_selection_remove(vf, vfi->focus_id, SELECTION_FOCUS, NULL);
    }

    if (!vficon_find_position(vf, id, &row, &col))
    {
        vfi->focus_id = NULL;
        vfi->focus_row = -1;
        vfi->focus_column = -1;
        return;
    }

    vfi->focus_id = id;
    vfi->focus_row = row;
    vfi->focus_column = col;
    vficon_selection_add(vf, vfi->focus_id, SELECTION_FOCUS, NULL);

    if (vficon_find_iter(vf, vfi->focus_id, &iter, NULL))
    {
        GtkTreePath *tpath;
        GtkTreeViewColumn *column;
        GtkTreeModel *store;

        tree_view_row_make_visible(GTK_TREE_VIEW(vf->listview), &iter, FALSE);

        store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
        tpath = gtk_tree_model_get_path(store, &iter);
        /* focus is set to an extra column with 0 width to hide focus, we draw it ourself */
        column = gtk_tree_view_get_column(GTK_TREE_VIEW(vf->listview), VFICON_MAX_COLUMNS);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(vf->listview), tpath, column, FALSE);
        gtk_tree_path_free(tpath);
    }
}

/* used to figure the page up/down distances */
static gint page_height(ViewFile *vf)
{
    GtkAdjustment *adj;
    gint page_size;
    gint row_height;
    gint ret;

    adj = gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(vf->listview));
    page_size = (gint)gtk_adjustment_get_page_increment(adj);

    row_height = options->thumbnails.max_height + THUMB_BORDER_PADDING * 2;
    if (VFICON(vf)->show_text) row_height += options->thumbnails.max_height / 3;

    ret = page_size / row_height;
    if (ret < 1) ret = 1;

    return ret;
}

/*
 *-------------------------------------------------------------------
 * keyboard
 *-------------------------------------------------------------------
 */

static void vfi_menu_position_cb(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer data)
{
    ViewFile *vf = data;
    GtkTreeModel *store;
    GtkTreeIter iter;
    gint column;
    GtkTreePath *tpath;
    gint cw, ch;

    if (!vficon_find_iter(vf, VFICON(vf)->click_id, &iter, &column)) return;
    store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
    tpath = gtk_tree_model_get_path(store, &iter);
    tree_view_get_cell_clamped(GTK_TREE_VIEW(vf->listview), tpath, column, FALSE, x, y, &cw, &ch);
    gtk_tree_path_free(tpath);
    *y += ch;
    popup_menu_position_clamp(menu, x, y, 0);
}

gboolean vficon_press_key_cb(GtkWidget *widget, GdkEventKey *event, ViewFile *vf)
{
    ViewFileInfoIcon *vfi = VFICON(vf);
    gint focus_row = 0;
    gint focus_col = 0;
    IconData *id;
    gboolean stop_signal;

    stop_signal = TRUE;
    switch (event->keyval)
    {
        case GDK_KEY_Left:      case GDK_KEY_KP_Left:      focus_col = -1; break;
        case GDK_KEY_Right:     case GDK_KEY_KP_Right:     focus_col =  1; break;
        case GDK_KEY_Up:        case GDK_KEY_KP_Up:        focus_row = -1; break;
        case GDK_KEY_Down:      case GDK_KEY_KP_Down:      focus_row =  1; break;
        case GDK_KEY_Page_Up:   case GDK_KEY_KP_Page_Up:   focus_row = -page_height(vf); break;
        case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: focus_row =  page_height(vf); break;

        case GDK_KEY_Home: case GDK_KEY_KP_Home:
            focus_row = -vfi->focus_row;
            focus_col = -vfi->focus_column;
            break;
        case GDK_KEY_End: case GDK_KEY_KP_End:
            focus_row = vfi->rows    - 1 - vfi->focus_row;
            focus_col = vfi->columns - 1 - vfi->focus_column;
            break;
        case GDK_KEY_space:
            id = vficon_find_data(vf, vfi->focus_row, vfi->focus_column, NULL);
            if (id)
            {
                vfi->click_id = id;
                if (event->state & GDK_CONTROL_MASK)
                {
                    gint selected;

                    selected = id->selected & SELECTION_SELECTED;
                    if (selected)
                    {
                        vficon_unselect(vf, id);
                    }
                    else
                    {
                        vficon_select(vf, id);
                        vf_send_layout_select(vf, id->fd);
                    }
                }
                else
                {
                    vf_select_none(vf);
                    vficon_select(vf, id);
                    vf_send_layout_select(vf, id->fd);
                }
            }
            break;
        case GDK_KEY_Menu:
            id = vficon_find_data(vf, vfi->focus_row, vfi->focus_column, NULL);
            vfi->click_id = id;

            vficon_selection_add(vf, vfi->click_id, SELECTION_PRELIGHT, NULL);
            tip_unschedule(vf);

            vf->popup = vf_pop_menu(vf);
            gtk_menu_popup(GTK_MENU(vf->popup), NULL, NULL, vfi_menu_position_cb,
                           vf, 0, event->time);
            break;
        default:
            stop_signal = FALSE;
            break;
    }

    if (focus_row != 0 || focus_col != 0)
    {
        IconData *new_id, *old_id;

        old_id = vficon_find_data(vf, vfi->focus_row, vfi->focus_column, NULL);
        vficon_move_focus(vf, focus_row, focus_col, TRUE);
        new_id = vficon_find_data(vf, vfi->focus_row, vfi->focus_column, NULL);

        if (new_id != old_id)
        {
            if (event->state & GDK_SHIFT_MASK)
            {
                if (!options->collections.rectangular_selection)
                    vficon_select_region_util(vf, old_id, new_id, FALSE);
                else
                    vficon_select_region_util(vf, vfi->click_id, old_id, FALSE);

                vficon_select_region_util(vf, vfi->click_id, new_id, TRUE);
                vf_send_layout_select(vf, new_id->fd);
            }
            else if (event->state & GDK_CONTROL_MASK)
            {
                vfi->click_id = new_id;
            }
            else
            {
                vfi->click_id = new_id;
                vf_select_none(vf);
                vficon_select(vf, new_id);
                vf_send_layout_select(vf, new_id->fd);
            }
        }
    }
    if (stop_signal)
        tip_unschedule(vf);

    return stop_signal;
}

/*
 *-------------------------------------------------------------------
 * mouse
 *-------------------------------------------------------------------
 */

static gboolean vficon_motion_cb(GtkWidget *widget, GdkEventMotion *event, ViewFile *vf)
{
    gint x = event->x,
         y = event->y,
         x_root = event->x_root,
         y_root = event->y_root;

    tip_update(vf, x, y, x_root, y_root);

    return FALSE;
}

gboolean vficon_press_cb(GtkWidget *widget, GdkEventButton *bevent, ViewFile *vf)
{
    ViewFileInfoIcon *vfi = VFICON(vf);
    GtkTreeIter iter;
    IconData *id;

    tip_unschedule(vf);

    id = vficon_find_data_by_coord(vf, (gint)bevent->x, (gint)bevent->y, &iter);

    vfi->click_id = id;
    vficon_selection_add(vf, vfi->click_id, SELECTION_PRELIGHT, &iter);

    switch (bevent->button)
    {
        case MOUSE_BUTTON_LEFT:
            if (!gtk_widget_has_focus(vf->listview))
                gtk_widget_grab_focus(vf->listview);

            if (bevent->type == GDK_2BUTTON_PRESS &&
                vf->layout)
            {
                vficon_selection_remove(vf, vfi->click_id, SELECTION_PRELIGHT, &iter);
                layout_image_full_screen_start(vf->layout, FALSE);
            }
            break;
        case MOUSE_BUTTON_RIGHT:
            vf->popup = vf_pop_menu(vf);
            gtk_menu_popup(GTK_MENU(vf->popup), NULL, NULL, popup_menu_at_event,
                           bevent, bevent->button, bevent->time);
            break;
        default:
            break;
    }

    return FALSE;
}

gboolean vficon_release_cb(GtkWidget *widget, GdkEventButton *bevent, ViewFile *vf)
{
    ViewFileInfoIcon *vfi = VFICON(vf);
    GtkTreeIter iter;
    IconData *id = NULL;
    gboolean was_selected;
    gint x = bevent->x,
         y = bevent->y,
         x_root = bevent->x_root,
         y_root = bevent->y_root;

    tip_schedule(vf, x, y, x_root, y_root);

    if ((gint)bevent->x != 0 || (gint)bevent->y != 0)
        id = vficon_find_data_by_coord(vf, x, y, &iter);

    if (vfi->click_id)
        vficon_selection_remove(vf, vfi->click_id, SELECTION_PRELIGHT, NULL);

    if (!id || vfi->click_id != id) return TRUE;

    was_selected = !!(id->selected & SELECTION_SELECTED);

    switch (bevent->button)
    {
        case MOUSE_BUTTON_LEFT:
            vficon_set_focus(vf, id);

            if (bevent->state & GDK_CONTROL_MASK)
            {
                gboolean select;

                select = !(id->selected & SELECTION_SELECTED);
                if ((bevent->state & GDK_SHIFT_MASK) && vfi->prev_selection)
                    vficon_select_region_util(vf, vfi->prev_selection, id, select);
                else
                    vficon_select_util(vf, id, select);
            }
            else
            {
                vf_select_none(vf);

                if ((bevent->state & GDK_SHIFT_MASK) && vfi->prev_selection)
                {
                    vficon_select_region_util(vf, vfi->prev_selection, id, TRUE);
                }
                else
                {
                    vficon_select_util(vf, id, TRUE);
                    was_selected = FALSE;
                }
            }
            break;
        case MOUSE_BUTTON_MIDDLE:
            vficon_select_util(vf, id, !(id->selected & SELECTION_SELECTED));
            break;
        default:
            break;
    }

    if (!was_selected && (id->selected & SELECTION_SELECTED))
        vf_send_layout_select(vf, id->fd);

    return TRUE;
}

static gboolean vficon_leave_cb(GtkWidget *widget, GdkEventCrossing *event, ViewFile *vf)
{
    tip_unschedule(vf);
    return FALSE;
}

/*
 *-------------------------------------------------------------------
 * population
 *-------------------------------------------------------------------
 */

static gboolean vficon_destroy_node_cb(GtkTreeModel *store, GtkTreePath *tpath,
                                       GtkTreeIter *iter, gpointer data)
{
    GList *list;

    gtk_tree_model_get(store, iter, FILE_COLUMN_POINTER, &list, -1);

    /* it seems that gtk_list_store_clear may call some callbacks
       that use the column. Set the pointer to NULL to be safe. */
    gtk_list_store_set(GTK_LIST_STORE(store), iter, FILE_COLUMN_POINTER, NULL, -1);
    g_list_free(list);

    return FALSE;
}

static void vficon_clear_store(ViewFile *vf)
{
    GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
    gtk_tree_model_foreach(store, vficon_destroy_node_cb, NULL);

    gtk_list_store_clear(GTK_LIST_STORE(store));
}

static GList *vficon_add_row(ViewFile *vf, GtkTreeIter *iter)
{
    GList *list = NULL;
    gint i;

    for (i = 0; i < VFICON(vf)->columns; i++)
        list = g_list_prepend(list, NULL);

    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));
    gtk_list_store_append(store, iter);
    gtk_list_store_set(store, iter, FILE_COLUMN_POINTER, list, -1);

    return list;
}

static void vficon_populate(ViewFile *vf, gboolean resize, gboolean keep_position)
{
    GtkTreePath *tpath;
    IconData *visible_id = NULL;
    gint r, c;
    gboolean valid;
    GtkTreeIter iter;

    vficon_verify_selections(vf);

    GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

    if (keep_position && gtk_widget_get_realized(vf->listview) &&
        gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview),
                                      0, 0, &tpath, NULL, NULL, NULL))
    {
        GtkTreeIter iter;
        GList *list;

        gtk_tree_model_get_iter(store, &iter, tpath);
        gtk_tree_path_free(tpath);

        gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
        if (list) visible_id = list->data;
    }


    if (resize)
    {
        vficon_clear_store(vf);

        gint thumb_width = vficon_get_icon_width(vf);

        for (gint i = 0; i < VFICON_MAX_COLUMNS; i++)
        {
            GtkTreeViewColumn *column;
            GtkCellRenderer *cell;
            GList *list;

            column = gtk_tree_view_get_column(GTK_TREE_VIEW(vf->listview), i);
            gtk_tree_view_column_set_visible(column, (i < VFICON(vf)->columns));
            gtk_tree_view_column_set_fixed_width(column, thumb_width + (THUMB_BORDER_PADDING * 6));

            list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
            cell = (list) ? list->data : NULL;
            g_list_free(list);

            if (cell && GQV_IS_CELL_RENDERER_ICON(cell))
            {
                g_object_set(G_OBJECT(cell),
                             "fixed_width",  thumb_width,
                             "fixed_height", options->thumbnails.max_height,
                             "show_text",    VFICON(vf)->show_text,
                             "show_marks",   vf->marks_enabled,
                             "num_marks",    FILEDATA_MARKS_SIZE,
                             NULL);
            }
        }
        if (gtk_widget_get_realized(vf->listview)) gtk_tree_view_columns_autosize(GTK_TREE_VIEW(vf->listview));
    }

    r = -1;

    valid = gtk_tree_model_iter_children(store, &iter, NULL);

    GList *work = vf->list;
    while (work)
    {
        GList *list;
        r++;
        if (valid)
        {
            gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
            gtk_list_store_set(GTK_LIST_STORE(store), &iter, FILE_COLUMN_POINTER, list, -1);
        }
        else
        {
            list = vficon_add_row(vf, &iter);
        }

        while (list)
        {
            IconData *id = NULL;

            if (work)
            {
                id = work->data;
                work = work->next;
            }
            list->data = id;
            list = list->next;
        }
        if (valid) valid = gtk_tree_model_iter_next(store, &iter);
    }

    r++;
    while (valid)
    {
        GList *list;

        gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
        valid = gtk_list_store_remove(GTK_LIST_STORE(store), &iter);
        g_list_free(list);
    }

    VFICON(vf)->rows = r;

    if (visible_id &&
        gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview),
                                      0, 0, &tpath, NULL, NULL, NULL))
    {
        GtkTreeIter iter;
        GList *list;

        gtk_tree_model_get_iter(store, &iter, tpath);
        gtk_tree_path_free(tpath);

        gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
        if (g_list_find(list, visible_id) == NULL &&
            vficon_find_iter(vf, visible_id, &iter, NULL))
        {
            tree_view_row_make_visible(GTK_TREE_VIEW(vf->listview), &iter, FALSE);
        }
    }
    vf_send_update(vf);
    vf_thumb_update(vf);
}

static void vficon_populate_at_new_size(ViewFile *vf, gint w, gint h, gboolean force)
{
    gint new_cols;
    gint thumb_width;

    thumb_width = vficon_get_icon_width(vf);

    new_cols = w / (thumb_width + (THUMB_BORDER_PADDING * 6));
    if (new_cols < 1) new_cols = 1;

    if (!force && new_cols == VFICON(vf)->columns) return;

    VFICON(vf)->columns = new_cols;

    vficon_populate(vf, TRUE, TRUE);

    DEBUG_1("col tab pop cols=%d rows=%d", VFICON(vf)->columns, VFICON(vf)->rows);
}

static void vficon_sized_cb(GtkWidget *widget, GtkAllocation *allocation, ViewFile *vf)
{
    vficon_populate_at_new_size(vf, allocation->width, allocation->height, FALSE);
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

void vficon_sort_set(ViewFile *vf, SortType type, gboolean ascend)
{
    if (vf->sort_method == type && vf->sort_ascend == ascend) return;

    vf->sort_method = type;
    vf->sort_ascend = ascend;

    if (vf->list)
        vf_refresh(vf);
}

/*
 *-----------------------------------------------------------------------------
 * thumb updates
 *-----------------------------------------------------------------------------
 */

void vficon_set_thumb_fd(ViewFile *vf, FileData *fd)
{
    GtkTreeIter iter;
    GList *list;

    if (!vficon_find_iter(vf, vficon_icon_data(vf, fd), &iter, NULL)) return;

    GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

    gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
    gtk_list_store_set(GTK_LIST_STORE(store), &iter, FILE_COLUMN_POINTER, list, -1);
}


FileData *vficon_thumb_next_fd(ViewFile *vf)
{
    GtkTreePath *tpath;
    FileData *fd = NULL;

    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview),
                                      0, 0, &tpath, NULL, NULL, NULL))
    {
        GtkTreeIter iter;
        gboolean valid = TRUE;

        GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
        gtk_tree_model_get_iter(store, &iter, tpath);
        gtk_tree_path_free(tpath);

        while (!fd && valid && tree_view_row_get_visibility(GTK_TREE_VIEW(vf->listview), &iter, FALSE) == 0)
        {
            GList *list;

            for (gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
                 list;
                 list = list->next)
            {
                IconData *id = list->data;
                if (id && !id->fd->thumb_pixbuf)
                {
                    fd = id->fd;
                    break;
                }
            }

            valid = gtk_tree_model_iter_next(store, &iter);
        }
    }

    if (fd)
        return fd;

    /* then find first undone */
    for (GList *work = vf->list; work; work = work->next)
    {
        IconData *id = work->data;
        FileData *fd_p = id->fd;

        if (!fd_p->thumb_pixbuf)
            return fd_p;
    }
    return NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 *-----------------------------------------------------------------------------
 */

static gboolean vficon_refresh_real(ViewFile *vf, gboolean keep_position)
{
    ViewFileInfoIcon *vfi = VFICON(vf);
    gboolean ret = TRUE;
    GList *work, *work_fd;
    GList *new_filelist = NULL;
    GList *new_iconlist = NULL;
    IconData *focus_id;
    FileData *first_selected = NULL;

    focus_id = vfi->focus_id;

    if (vf->dir_fd)
    {
        ret = filelist_read(vf->dir_fd, &new_filelist, NULL);
        new_filelist = file_data_filter_marks_list(new_filelist, vf_marks_get_filter(vf));
    }

    /* the list might not be sorted if there were renames */
    vf->list =     iconlist_sort(vf->list,     vf->sort_method, vf->sort_ascend);
    new_filelist = filelist_sort(new_filelist, vf->sort_method, vf->sort_ascend);

    if (vfi->selection)
    {
        first_selected = ((IconData *)(vfi->selection->data))->fd;
        file_data_ref(first_selected);
        g_clear_list(&vfi->selection, NULL);
    }

    /* check for same files from old_list */
    work = vf->list;
    work_fd = new_filelist;
    while (work || work_fd)
    {
        IconData *id = NULL;
        FileData *fd = NULL;
        FileData *new_fd = NULL;
        gint match;

        if (work && work_fd)
        {
            id = work->data;
            fd = id->fd;

            new_fd = work_fd->data;

            if (fd == new_fd)
            {
                /* not changed, go to next */
                work = work->next;
                work_fd = work_fd->next;
                if (id->selected & SELECTION_SELECTED)
                    vfi->selection = g_list_prepend(vfi->selection, id);

                continue;
            }

            match = filelist_sort_compare_filedata(fd, new_fd, vf->sort_method, vf->sort_ascend);
            if (match == 0) g_warning("multiple fd for the same path");
        }
        else if (work)
        {
            id = work->data;
            fd = id->fd;
            match = -1;
        }
        else /* work_fd */
        {
            new_fd = work_fd->data;
            match = 1;
        }

        if (match < 0)
        {
            /* file no longer exists, delete from vf->list */
            GList *to_delete = work;
            work = work->next;
            if (id == vfi->prev_selection) vfi->prev_selection = NULL;
            if (id == vfi->click_id) vfi->click_id = NULL;
            file_data_unref(fd);
            g_free(id);
            vf->list = g_list_delete_link(vf->list, to_delete);
        }
        else
        {
            /* new file, add to vf->list */
            id = g_new0(IconData, 1);

            id->selected = SELECTION_NONE;
            id->fd = file_data_ref(new_fd);
            if (work)
                vf->list = g_list_insert_before(vf->list, work, id);
            else
                /* it is faster to append all new entries together later */
                new_iconlist = g_list_prepend(new_iconlist, id);

            work_fd = work_fd->next;
        }

    }

    if (new_iconlist)
        vf->list = g_list_concat(vf->list, g_list_reverse(new_iconlist));

    vfi->selection = g_list_reverse(vfi->selection);

    filelist_free(new_filelist);

    vficon_populate(vf, TRUE, keep_position);

    if (first_selected && !vfi->selection)
    {
        /* all selected files disappeared */
        IconData *closest = (IconData *)vf_find_closest_entry(vf, first_selected);
        if (closest)
        {
            vficon_select(vf, closest);
            vf_send_layout_select(vf, closest->fd);
        }
    }
    file_data_unref(first_selected);

    /* attempt to keep focus on same icon when refreshing */
    if (focus_id && g_list_find(vf->list, focus_id))
        vficon_set_focus(vf, focus_id);

    return ret;
}

gboolean vficon_refresh(ViewFile *vf)
{
    return vficon_refresh_real(vf, TRUE);
}

/*
 *-----------------------------------------------------------------------------
 * draw, etc.
 *-----------------------------------------------------------------------------
 */

typedef struct ColumnData ColumnData;
struct ColumnData
{
    ViewFile *vf;
    gint number;
};

static void vficon_cell_data_cb(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
                                GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
    GList *list;
    IconData *id;
    ColumnData *cd = data;
    ViewFile *vf = cd->vf;

    if (!GQV_IS_CELL_RENDERER_ICON(cell)) return;

    gtk_tree_model_get(tree_model, iter, FILE_COLUMN_POINTER, &list, -1);

    id = g_list_nth_data(list, cd->number);

    if (id)
    {
        GdkColor color_fg;
        GdkColor color_bg;
        GtkStyle *style;
        gchar *name_sidecars;
        gchar *link;
        GtkStateType state = GTK_STATE_NORMAL;

        g_assert(id->fd->magick == FD_MAGICK);

        link = islink(id->fd->path) ? GQ_LINK_STR : "";
        if (id->fd->sidecar_files)
        {
            gchar *sidecars = file_data_sc_list_to_string(id->fd);
            name_sidecars = g_strdup_printf("%s%s %s", link, id->fd->name, sidecars);
            g_free(sidecars);
        }
        else
        {
            gchar *disabled_grouping = id->fd->disable_grouping ? _(" [NO GROUPING]") : "";
            name_sidecars = g_strdup_printf("%s%s%s", link, id->fd->name, disabled_grouping);
        }

        style = gtk_widget_get_style(vf->listview);
        if (id->selected & SELECTION_SELECTED)
            state = GTK_STATE_SELECTED;

        memcpy(&color_fg, &style->text[state], sizeof(color_fg));
        memcpy(&color_bg, &style->base[state], sizeof(color_bg));

        if (id->selected & SELECTION_PRELIGHT)
            shift_color(&color_bg, -1, 0);

        g_object_set(cell,
                     "pixbuf",              id->fd->thumb_pixbuf,
                     "text",                name_sidecars,
                     "marks",               file_data_get_marks(id->fd),
                     "show_marks",          vf->marks_enabled,
                     "cell-background-gdk", &color_bg,
                     "cell-background-set", TRUE,
                     "foreground-gdk",      &color_fg,
                     "foreground-set",      TRUE,
                     "has-focus",           (VFICON(vf)->focus_id == id),
                     NULL);
        g_free(name_sidecars);
    }
    else
    {
        g_object_set(cell,
                     "pixbuf",              NULL,
                     "text",                NULL,
                     "show_marks",          FALSE,
                     "cell-background-set", FALSE,
                     "foreground-set",      FALSE,
                     "has-focus",           FALSE,
                     NULL);
    }
}

static void vficon_append_column(ViewFile *vf, gint n)
{
    ColumnData *cd;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_min_width(column, 0);

    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_alignment(column, 0.5);

    renderer = gqv_cell_renderer_icon_new();
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    g_object_set(G_OBJECT(renderer),
                 "xpad", THUMB_BORDER_PADDING * 2,
                 "ypad", THUMB_BORDER_PADDING,
                 "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
                 NULL);

    g_object_set_data(G_OBJECT(column),   "column_number", GINT_TO_POINTER(n));
    g_object_set_data(G_OBJECT(renderer), "column_number", GINT_TO_POINTER(n));

    cd = g_new0(ColumnData, 1);
    cd->vf = vf;
    cd->number = n;
    gtk_tree_view_column_set_cell_data_func(column, renderer, vficon_cell_data_cb, cd, g_free);

    gtk_tree_view_append_column(GTK_TREE_VIEW(vf->listview), column);

    g_signal_connect(G_OBJECT(renderer), "toggled", G_CALLBACK(vficon_mark_toggled_cb), vf);
}

/*
 *-----------------------------------------------------------------------------
 * base
 *-----------------------------------------------------------------------------
 */

static FileData *vficon_op_item_fd(gpointer item)
{
    return item ? ((IconData *)item)->fd : NULL;
}

static void vficon_op_color_set(ViewFile *vf, FileData *fd, gboolean enable)
{
    /* no op */
}

static FileData *vficon_op_fd_at_coord(ViewFile *vf, gint x, gint y)
{
    IconData *id = vficon_find_data_by_coord(vf, x, y, NULL);
    return id ? id->fd : NULL;
}

static void vficon_op_drag_started(ViewFile *vf)
{
    tip_unschedule(vf);
}

static void vficon_op_drag_ended(ViewFile *vf)
{
    vficon_selection_remove(vf, VFICON(vf)->click_id, SELECTION_PRELIGHT, NULL);
    tip_unschedule(vf);
}

static const ViewFileFuncs vficon_funcs = {
    .item_fd         = vficon_op_item_fd,
    .fd_selected     = vficon_fd_selected,
    .fd_mark_updated = vficon_fd_mark_updated,
    .fd_at_coord     = vficon_op_fd_at_coord,
    .clicked_fd      = vficon_clicked_fd,
    .clicked_clear   = vficon_clicked_clear,
    .color_set       = vficon_op_color_set,
    .drag_started    = vficon_op_drag_started,
    .drag_ended      = vficon_op_drag_ended,
};

gboolean vficon_set_fd(ViewFile *vf, FileData *dir_fd)
{
    gboolean ret;

    if (!dir_fd) return FALSE;
    if (vf->dir_fd == dir_fd) return TRUE;

    file_data_unref(vf->dir_fd);
    vf->dir_fd = file_data_ref(dir_fd);

    g_clear_list(&VFICON(vf)->selection, NULL);
    g_clear_list(&vf->list, iconlist_free_item);

    /* NOTE: populate will clear the store for us */
    ret = vficon_refresh_real(vf, FALSE);

    VFICON(vf)->focus_id = NULL;
    vficon_move_focus(vf, 0, 0, FALSE);

    return ret;
}

void vficon_destroy_cb(GtkWidget *widget, ViewFile *vf)
{
    vf_refresh_idle_cancel(vf);

    file_data_unregister_notify_func(vf_notify_cb, vf);

    tip_unschedule(vf);

    vf_thumb_cleanup(vf);

    g_clear_list(&vf->list, iconlist_free_item);
    g_list_free(VFICON(vf)->selection);
}

ViewFile *vficon_new(ViewFile *vf, FileData *dir_fd)
{
    GtkListStore *store;
    GtkTreeSelection *selection;
    gint i;

    vf->info = g_new0(ViewFileInfoIcon, 1);
    vf->funcs = &vficon_funcs;

    VFICON(vf)->show_text = options->show_icon_names;

    store = gtk_list_store_new(1, G_TYPE_POINTER);
    vf->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
    gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_NONE);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(vf->listview), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(vf->listview), FALSE);

    for (i = 0; i < VFICON_MAX_COLUMNS; i++)
        vficon_append_column(vf, i);

    /* zero width column to hide tree view focus, we draw it ourselves */
    vficon_append_column(vf, i);
    /* end column to fill white space */
    vficon_append_column(vf, i);

    g_signal_connect(G_OBJECT(vf->listview), "size_allocate",
                     G_CALLBACK(vficon_sized_cb), vf);

    gtk_widget_set_events(vf->listview,
                          GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK |
                          GDK_BUTTON_PRESS_MASK | GDK_LEAVE_NOTIFY_MASK);

    g_signal_connect(G_OBJECT(vf->listview),"motion_notify_event",
                     G_CALLBACK(vficon_motion_cb), vf);
    g_signal_connect(G_OBJECT(vf->listview), "leave_notify_event",
                     G_CALLBACK(vficon_leave_cb), vf);

    /* force VFICON(vf)->columns to be at least 1 (sane) - this will be corrected in the size_cb */
    vficon_populate_at_new_size(vf, 1, 1, FALSE);

    file_data_register_notify_func(vf_notify_cb, vf, NOTIFY_PRIORITY_MEDIUM);

    return vf;
}

