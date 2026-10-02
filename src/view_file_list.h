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

#ifndef VIEW_FILE_LIST_H
#define VIEW_FILE_LIST_H

#include "filedata.h"

struct FileData;
struct ViewFile;

gboolean vflist_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data);
gboolean vflist_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data);
gboolean vflist_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data);

void vflist_destroy_cb(GtkWidget *widget, gpointer data);
struct ViewFile *vflist_new(struct ViewFile *vf, struct FileData *dir_fd);

gboolean vflist_set_fd(struct ViewFile *vf, struct FileData *dir_fd);
gboolean vflist_refresh(struct ViewFile *vf);

void vflist_thumb_set(struct ViewFile *vf, gboolean enable);
void vflist_marks_set(struct ViewFile *vf, gboolean enable);
void vflist_sort_set(struct ViewFile *vf, SortType type, gboolean ascend);

GList *vflist_selection_get_one(struct ViewFile *vf, struct FileData *fd);
void vflist_pop_menu_rename_cb(GtkWidget *widget, gpointer data);
void vflist_pop_menu_thumbs_cb(GtkWidget *widget, gpointer data);
void vflist_clicked_clear(struct ViewFile *vf);
gboolean vflist_rename_in_place(struct ViewFile *vf);

gboolean vflist_row_is_selected(struct ViewFile *vf, struct FileData *fd);
gboolean vflist_index_is_selected(struct ViewFile *vf, gint row);
guint vflist_selection_count(struct ViewFile *vf, gint64 *bytes);
GList *vflist_selection_get_list(struct ViewFile *vf);
GList *vflist_selection_get_list_by_index(struct ViewFile *vf);

void vflist_select_all(struct ViewFile *vf);
void vflist_select_none(struct ViewFile *vf);
void vflist_select_invert(struct ViewFile *vf);
void vflist_select_by_fd(struct ViewFile *vf, struct FileData *fd);

void vflist_mark_to_selection(struct ViewFile *vf, gint mark, MarkToSelectionMode mode);

void vflist_color_set(struct ViewFile *vf, struct FileData *fd, gboolean color_set);

void vflist_set_thumb_fd(struct ViewFile *vf, struct FileData *fd);
struct FileData *vflist_thumb_next_fd(struct ViewFile *vf);

#endif
