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

gboolean vficon_press_key_cb(GtkWidget *widget, GdkEventKey *event, ViewFile *vf);
gboolean vficon_press_cb(GtkWidget *widget, GdkEventButton *bevent, ViewFile *vf);
gboolean vficon_release_cb(GtkWidget *widget, GdkEventButton *bevent, ViewFile *vf);

void vficon_destroy_cb(GtkWidget *widget, ViewFile *vf);
ViewFile *vficon_new(ViewFile *vf, FileData *dir_fd);

gboolean vficon_set_fd(ViewFile *vf, FileData *dir_fd);
gboolean vficon_refresh(ViewFile *vf);

void vficon_sort_set(ViewFile *vf, SortType type, gboolean ascend);

void vficon_marks_set(ViewFile *vf, gboolean enable);

FileData *vficon_clicked_fd(ViewFile *vf);
gboolean vficon_fd_selected(ViewFile *vf, FileData *fd);
GList *vficon_selection_get_one(ViewFile *vf, FileData *fd);
void vficon_clicked_clear(ViewFile *vf);
void vficon_pop_menu_rename_cb(GtkWidget *widget, ViewFile *vf);
void vficon_pop_menu_show_names_cb(GtkWidget *widget, ViewFile *vf);

gboolean vficon_index_is_selected(ViewFile *vf, gint row);
guint vficon_selection_count(ViewFile *vf, gint64 *bytes);
GList *vficon_selection_get_list(ViewFile *vf);
GList *vficon_selection_get_list_by_index(ViewFile *vf);

void vficon_select_all(ViewFile *vf);
void vficon_select_none(ViewFile *vf);
void vficon_select_invert(ViewFile *vf);
void vficon_select_by_fd(ViewFile *vf, FileData *fd);

void vficon_mark_to_selection(ViewFile *vf, gint mark, MarkToSelectionMode mode);

void vficon_set_thumb_fd(ViewFile *vf, FileData *fd);
FileData *vficon_thumb_next_fd(ViewFile *vf);

#endif
