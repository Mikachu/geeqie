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

#ifndef VIEW_FILE_ICON_H
#define VIEW_FILE_ICON_H

#include "filedata.h"

struct FileData;
struct ViewFile;

gboolean vficon_press_key_cb(GtkWidget *widget, GdkEventKey *event, struct ViewFile *vf);
gboolean vficon_press_cb(GtkWidget *widget, GdkEventButton *bevent, struct ViewFile *vf);
gboolean vficon_release_cb(GtkWidget *widget, GdkEventButton *bevent, struct ViewFile *vf);

void vficon_destroy_cb(GtkWidget *widget, struct ViewFile *vf);
struct ViewFile *vficon_new(struct ViewFile *vf, struct FileData *dir_fd);

gboolean vficon_set_fd(struct ViewFile *vf, struct FileData *dir_fd);
gboolean vficon_refresh(struct ViewFile *vf);

void vficon_sort_set(struct ViewFile *vf, SortType type, gboolean ascend);

void vficon_marks_set(struct ViewFile *vf, gboolean enable);

struct FileData *vficon_clicked_fd(struct ViewFile *vf);
gboolean vficon_fd_selected(struct ViewFile *vf, struct FileData *fd);
GList *vficon_selection_get_one(struct ViewFile *vf, struct FileData *fd);
void vficon_clicked_clear(struct ViewFile *vf);
void vficon_pop_menu_rename_cb(GtkWidget *widget, struct ViewFile *vf);
void vficon_pop_menu_show_names_cb(GtkWidget *widget, struct ViewFile *vf);

gboolean vficon_index_is_selected(struct ViewFile *vf, gint row);
guint vficon_selection_count(struct ViewFile *vf, gint64 *bytes);
GList *vficon_selection_get_list(struct ViewFile *vf);
GList *vficon_selection_get_list_by_index(struct ViewFile *vf);

void vficon_select_all(struct ViewFile *vf);
void vficon_select_none(struct ViewFile *vf);
void vficon_select_invert(struct ViewFile *vf);
void vficon_select_by_fd(struct ViewFile *vf, struct FileData *fd);

void vficon_mark_to_selection(struct ViewFile *vf, gint mark, MarkToSelectionMode mode);

void vficon_set_thumb_fd(struct ViewFile *vf, struct FileData *fd);
struct FileData *vficon_thumb_next_fd(struct ViewFile *vf);

#endif
