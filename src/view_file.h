/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Laurent Monin
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

#ifndef VIEW_FILE_H
#define VIEW_FILE_H

#define VIEW_FILE_TYPES_COUNT 2

#define VFLIST(_vf_) ((ViewFileInfoList *)(_vf_->info))
#define VFICON(_vf_) ((ViewFileInfoIcon *)(_vf_->info))

/* per-view-type operations; populated by view_file_list.c / view_file_icon.c
   and reached through vf->funcs, avoiding switch(vf->type) forks */
struct ViewFileFuncs
{
    /* unwrap a vf->list node's payload to FileData* */
    FileData *(*item_fd)(gpointer item);

    gboolean  (*fd_selected)(ViewFile *vf, FileData *fd);

    /* refresh display of fd after mark change */
    void (*fd_mark_updated)(ViewFile *vf, FileData *fd);

    FileData *(*fd_at_coord)(ViewFile *vf, gint x, gint y);
    FileData *(*clicked_fd)(ViewFile *vf);
    void      (*clicked_clear)(ViewFile *vf);

    void      (*color_set)(ViewFile *vf, FileData *fd, gboolean enable);

    void      (*drag_started)(ViewFile *vf);   /* per-type drag-begin extras */
    void      (*drag_ended)(ViewFile *vf);     /* per-type drag-end extras */
};

void vf_send_update(ViewFile *vf);
void vf_dnd_init(ViewFile *vf);

ViewFile *vf_new(FileViewType type, FileData *dir_fd);

void vf_set_status_func(ViewFile *vf, void (*func)(ViewFile *vf, gpointer data), gpointer data);
void vf_set_thumb_status_func(ViewFile *vf, void (*func)(ViewFile *vf, gdouble val,
                                                         const gchar *text, gpointer data), gpointer data);

void vf_set_layout(ViewFile *vf, LayoutWindow *layout);

gboolean vf_set_fd(ViewFile *vf, FileData *fd);
gboolean vf_refresh(ViewFile *vf);
void vf_refresh_idle(ViewFile *vf);

void vf_thumb_set(ViewFile *vf, gboolean enable);
void vf_marks_set(ViewFile *vf, gboolean enable);
void vf_sort_set(ViewFile *vf, SortType type, gboolean ascend);

gboolean vf_mts_select(FileData *fd, gint n, gboolean selected, MarkToSelectionMode mode);
guint vf_marks_get_filter(ViewFile *vf);
void vf_mark_filter_toggle(ViewFile *vf, gint mark);

GList *vf_selection_get_one(ViewFile *vf, FileData *fd);
GList *vf_pop_menu_file_list(ViewFile *vf);
GtkWidget *vf_pop_menu(ViewFile *vf);

FileData *vf_item_fd(ViewFile *vf, gpointer item);
void vf_fd_color_set(ViewFile *vf, FileData *fd, gboolean enable);
gboolean vf_fd_selected(ViewFile *vf, FileData *fd);
FileData *vf_clicked_fd(ViewFile *vf);
FileData *vf_index_get_data(ViewFile *vf, gint row);
gint vf_index_by_fd(ViewFile *vf, FileData *in_fd);
guint vf_count(ViewFile *vf, gint64 *bytes);
GList *vf_get_list(ViewFile *vf);
void vf_send_layout_select(ViewFile *vf, FileData *sel_fd);

gint vf_index_is_selected(ViewFile *vf, gint row);
guint vf_selection_count(ViewFile *vf, gint64 *bytes);
GList *vf_selection_get_list(ViewFile *vf);
GList *vf_selection_get_list_by_index(ViewFile *vf);
gpointer vf_find_closest_entry(ViewFile *vf, FileData *sel_fd);

void vf_select_all(ViewFile *vf);
void vf_select_none(ViewFile *vf);
void vf_select_invert(ViewFile *vf);
void vf_select_by_fd(ViewFile *vf, FileData *fd);

void vf_mark_to_selection(ViewFile *vf, gint mark, MarkToSelectionMode mode);
void vf_selection_to_mark(ViewFile *vf, gint mark, SelectionToMarkMode mode);

void vf_refresh_idle_cancel(ViewFile *vf);
void vf_notify_cb(FileData *fd, NotifyType type, gpointer data);

void vf_thumb_update(ViewFile *vf);
void vf_thumb_cleanup(ViewFile *vf);
void vf_thumb_stop(ViewFile *vf);

#endif /* VIEW_FILE_H */
