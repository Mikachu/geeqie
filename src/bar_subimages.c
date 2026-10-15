/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "main.h"
#include "bar_subimages.h"

#include "bar.h"
#include "filedata.h"
#include "filefilter.h"
#include "layout.h"
#include "layout_image.h"
#include "rcfile.h"
#include "ui_misc.h"

enum {
    SUBIMAGES_COLUMN_PAGE = 0,
    SUBIMAGES_COLUMN_NAME,
    SUBIMAGES_COLUMN_COUNT
};

typedef struct PaneSubimagesData PaneSubimagesData;
struct PaneSubimagesData
{
    PaneData pane;
    GtkWidget *widget;   /* scrolled window */
    GtkWidget *view;     /* GtkTreeView */
    FileData *fd, *built_fd;
    gboolean updating;   /* suppress "changed" during rebuild */
    gboolean new_file;
};

static void bar_pane_subimages_update(PaneSubimagesData *psd)
{
    GtkTreeView *view = GTK_TREE_VIEW(psd->view);

    if (psd->fd && psd->fd->page_total > 0 &&
        psd->fd == psd->built_fd)
    {
        GtkTreePath *path =
            gtk_tree_path_new_from_indices((gint) psd->fd->page_num, -1);
        psd->updating = TRUE;
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(view), path);
        psd->updating = FALSE;

        /* scroll only if the row fell outside the visible area */
        GtkTreePath *start, *end;
        if (!gtk_tree_view_get_visible_range(view, &start, &end) ||
            gtk_tree_path_compare(path, start) < 0 ||
            gtk_tree_path_compare(path, end) > 0)
        {
            gtk_tree_view_scroll_to_cell(view, path, NULL, FALSE, 0.0, 0.0);
        }
        if (start) gtk_tree_path_free(start);
        if (end)   gtk_tree_path_free(end);

        gtk_tree_path_free(path);
        return;
    }

    GtkListStore *store = gtk_list_store_new(SUBIMAGES_COLUMN_COUNT,
                                             G_TYPE_UINT, G_TYPE_STRING);
    gboolean is_video = psd->fd &&
                        filter_file_class(psd->fd->path, FORMAT_CLASS_VIDEO);

    psd->updating = TRUE;

    if (psd->fd && psd->fd->page_total > 0)
    {
        for (guint i = 0; i < psd->fd->page_total; i++)
        {
            gchar *name;
            GtkTreeIter iter;

            if (psd->fd->page_names && psd->fd->page_names[i])
                name = g_strdup(psd->fd->page_names[i]);
            else if (is_video)
                name = g_strdup_printf(_("Attachment %u"), i + 1);
            else
                name = g_strdup_printf(_("Page %u"), i + 1);

            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter,
                               SUBIMAGES_COLUMN_PAGE, i,
                               SUBIMAGES_COLUMN_NAME, name,
                               -1);
            g_free(name);
        }
    }
    else
    {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           SUBIMAGES_COLUMN_NAME, _("No sub-images"),
                           -1);
    }

    /* replacing the model clears the selection; re-select current page */
    gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));
    g_object_unref(store);

    /* make sure this is empty if we don't set it below */
    g_clear_pointer(&psd->built_fd, file_data_unref);

    if (psd->fd && psd->fd->page_total > 0)
    {
        GtkTreePath *path =
            gtk_tree_path_new_from_indices((gint) psd->fd->page_num, -1);
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(view), path);
        if (psd->new_file && psd->fd->page_num < psd->fd->page_total)
            gtk_tree_view_scroll_to_cell(view, path, NULL, FALSE, 0.0, 0.0);
        gtk_tree_path_free(path);
        psd->built_fd = file_data_ref(psd->fd);
    }

    psd->new_file = FALSE;
    psd->updating = FALSE;
}

static void bar_pane_subimages_selection_changed(GtkTreeSelection *sel, gpointer data)
{
    PaneSubimagesData *psd = data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    guint page;

    if (psd->updating || !psd->fd) return;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    gtk_tree_model_get(model, &iter, SUBIMAGES_COLUMN_PAGE, &page, -1);
    if (page == psd->fd->page_num) return;

    image_set_page(psd->pane.lw->image, page);
}

static void bar_pane_subimages_set_fd(GtkWidget *pane, FileData *fd)
{
    PaneSubimagesData *psd = g_object_get_data(G_OBJECT(pane), "pane_data");
    if (!psd) return;

    if (fd != psd->fd)
    {
        psd->new_file = TRUE;
        file_data_unref(psd->fd);
        psd->fd = file_data_ref(fd);
    }

    bar_pane_subimages_update(psd);
}

static void bar_pane_subimages_notify_cb(FileData *fd, NotifyType type,
                                         gpointer data)
{
    PaneSubimagesData *psd = data;
    if ((type & (NOTIFY_PIXBUF | NOTIFY_REREAD | NOTIFY_CHANGE)) &&
        fd == psd->fd)
    {
        DEBUG_1("Notify pane_subimages: %s %04x", fd->path, type);

        /* page_total/page_names are filled in by the loader backend
         * during decode, i.e. after set_fd ran; refresh again now */
        bar_pane_subimages_update(psd);
    }
}

static void bar_pane_subimages_write_config(GtkWidget *pane, GString *outstr,
                                            gint indent)
{
    PaneSubimagesData *psd = g_object_get_data(G_OBJECT(pane), "pane_data");
    if (!psd) return;

    WRITE_NL(); WRITE_STRING("<pane_subimages ");
    write_char_option(outstr, indent, "id", psd->pane.id);
    write_char_option(outstr, indent, "title",
                      gtk_label_get_text(GTK_LABEL(psd->pane.title)));
    WRITE_BOOL(psd->pane, expanded);
    WRITE_INT(psd->pane, height);
    WRITE_STRING("/>");
}

static void bar_pane_subimages_destroy(GtkWidget *widget, gpointer data)
{
    PaneSubimagesData *psd = data;

    file_data_unregister_notify_func(bar_pane_subimages_notify_cb, psd);

    file_data_unref(psd->fd);
    file_data_unref(psd->built_fd);
    g_free(psd->pane.id);
    g_free(psd);
}

static GtkWidget *bar_pane_subimages_new(const gchar *id, const gchar *title,
                                         gboolean expanded, gint height)
{
    PaneSubimagesData *psd = g_new0(PaneSubimagesData, 1);

    psd->pane.pane_set_fd       = bar_pane_subimages_set_fd;
    psd->pane.pane_write_config = bar_pane_subimages_write_config;
    psd->pane.title             = bar_pane_expander_title(title);
    psd->pane.id                = g_strdup(id);
    psd->pane.type              = PANE_SUBIMAGES;
    psd->pane.expanded          = expanded;
    psd->pane.height            = height;

    psd->new_file = TRUE;

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);

    psd->widget = scrolled;
    g_object_set_data(G_OBJECT(psd->widget), "pane_data", psd);
    g_signal_connect(G_OBJECT(psd->widget), "destroy",
                     G_CALLBACK(bar_pane_subimages_destroy), psd);

    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                        GTK_SHADOW_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(psd->widget, -1, height);
    gtk_widget_show(scrolled);

    psd->view = gtk_tree_view_new();
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(psd->view), FALSE);

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(psd->view));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);

    GtkCellRenderer *cell = gtk_cell_renderer_text_new();
    g_object_set(cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(col, cell, TRUE);
    gtk_tree_view_column_set_attributes(col, cell,
                                        "text", SUBIMAGES_COLUMN_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(psd->view), col);

    gtk_container_add(GTK_CONTAINER(scrolled), psd->view);
    g_signal_connect(G_OBJECT(sel), "changed",
                     G_CALLBACK(bar_pane_subimages_selection_changed), psd);
    gtk_widget_show(psd->view);

    file_data_register_notify_func(bar_pane_subimages_notify_cb, psd,
                                   NOTIFY_PRIORITY_LOW);

    return psd->widget;
}

GtkWidget *bar_pane_subimages_new_from_config(const gchar **attribute_names,
                                              const gchar **attribute_values)
{
    gchar *title = NULL;
    gboolean expanded = TRUE;
    gint height = 150;
    gchar *id = g_strdup("subimages");

    while (*attribute_names)
    {
        const gchar *option = *attribute_names++;
        const gchar *value = *attribute_values++;

        if (READ_CHAR_FULL("title",    title))    continue;
        if (READ_BOOL_FULL("expanded", expanded)) continue;
        if (READ_INT_FULL("height",    height))   continue;
        if (READ_CHAR_FULL("id",       id))       continue;

        log_printf("unknown attribute %s = %s\n", option, value);
    }
    bar_pane_translate_title(PANE_SUBIMAGES, id, &title);

    GtkWidget *pane = bar_pane_subimages_new(id, title, expanded, height);

    g_free(title);
    g_free(id);
    return pane;
}

void bar_pane_subimages_update_from_config(GtkWidget *pane,
                                           const gchar **attribute_names,
                                           const gchar **attribute_values)
{
    PaneSubimagesData *psd = g_object_get_data(G_OBJECT(pane), "pane_data");
    if (!psd) return;

    gchar *title = NULL;

    while (*attribute_names)
    {
        const gchar *option = *attribute_names++;
        const gchar *value = *attribute_values++;

        if (READ_CHAR_FULL("title",    title))              continue;
        if (READ_BOOL_FULL("expanded", psd->pane.expanded)) continue;
        if (READ_INT_FULL("height",    psd->pane.height))   continue;
        if (READ_CHAR_FULL("id",       psd->pane.id))       continue;

        log_printf("unknown attribute %s = %s\n", option, value);
    }

    if (title)
    {
        bar_pane_translate_title(PANE_SUBIMAGES, psd->pane.id, &title);
        gtk_label_set_text(GTK_LABEL(psd->pane.title), title);
        g_free(title);
    }
    gtk_widget_set_size_request(psd->widget, -1, psd->pane.height);
    bar_update_expander(pane);
    bar_pane_subimages_update(psd);
}
