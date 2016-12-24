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

#ifndef PAN_VIEW_PAN_VIEW_FILTER_H
#define PAN_VIEW_PAN_VIEW_FILTER_H

#include "main.h"
#include "pan-types.h"

typedef enum {
    PAN_VIEW_FILTER_REQUIRE,
    PAN_VIEW_FILTER_EXCLUDE,
    PAN_VIEW_FILTER_INCLUDE,
    PAN_VIEW_FILTER_GROUP
} PanViewFilterMode;

typedef struct _PanViewFilterElement PanViewFilterElement;
struct _PanViewFilterElement
{
    PanViewFilterMode mode;
    gchar *keyword;
    GRegex *kw_regex;
};

typedef struct _PanFilterCallbackState PanFilterCallbackState;
struct _PanFilterCallbackState
{
    PanWindow *pw;
    GList *filter_element;
};

struct _PanViewFilterUi
{
    GtkWidget *filter_box;
    GtkWidget *filter_entry;
    GtkWidget *filter_label;
    GtkWidget *filter_button;
    GtkWidget *filter_button_arrow;
    GtkWidget *filter_kw_hbox;
    GtkListStore *filter_mode_model;
    GtkWidget *filter_mode_combo;
    GList *filter_elements;  // List of PanViewFilterElement.
};

void pan_filter_toggle_visible(PanWindow *pw, gboolean enable);
void pan_filter_activate(PanWindow *pw);
void pan_filter_activate_cb(const gchar *text, gpointer data);
void pan_filter_toggle_cb(GtkWidget *button, gpointer data);

// Creates a new PanViewFilterUi instance and returns it.
PanViewFilterUi *pan_filter_ui_new(PanWindow *pw);

// Destroys the specified PanViewFilterUi and sets the pointer to NULL.
void pan_filter_ui_destroy(PanViewFilterUi **ui);

gboolean pan_filter_fd_list(GList **fd_list, GList *filter_elements);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
