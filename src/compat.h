/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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

#ifndef COMPAT_H
#define COMPAT_H


/* Some systems (BSD,MacOsX,HP-UX,...) define MAP_ANON and not MAP_ANONYMOUS */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS   MAP_ANON
#elif defined(MAP_ANONYMOUS) && !defined(MAP_ANON)
#define MAP_ANON    MAP_ANONYMOUS
#endif

#if !GTK_CHECK_VERSION(2,22,0)
#define GDK_KEY_BackSpace   GDK_BackSpace
#define GDK_KEY_Delete      GDK_Delete
#define GDK_KEY_Down        GDK_Down
#define GDK_KEY_End     GDK_End
#define GDK_KEY_Escape      GDK_Escape
#define GDK_KEY_F10     GDK_F10
#define GDK_KEY_F11     GDK_F11
#define GDK_KEY_Home        GDK_Home
#define GDK_KEY_ISO_Left_Tab    GDK_ISO_Left_Tab
#define GDK_KEY_KP_Add      GDK_KP_Add
#define GDK_KEY_KP_Delete   GDK_KP_Delete
#define GDK_KEY_KP_Divide   GDK_KP_Divide
#define GDK_KEY_KP_Down     GDK_KP_Down
#define GDK_KEY_KP_End      GDK_KP_End
#define GDK_KEY_KP_Enter    GDK_KP_Enter
#define GDK_KEY_KP_Home     GDK_KP_Home
#define GDK_KEY_KP_Left     GDK_KP_Left
#define GDK_KEY_KP_Multiply GDK_KP_Multiply
#define GDK_KEY_KP_Page_Down    GDK_KP_Page_Down
#define GDK_KEY_KP_Page_Up  GDK_KP_Page_Up
#define GDK_KEY_KP_Right    GDK_KP_Right
#define GDK_KEY_KP_Subtract GDK_KP_Subtract
#define GDK_KEY_KP_Up       GDK_KP_Up
#define GDK_KEY_Left        GDK_Left
#define GDK_KEY_Menu        GDK_Menu
#define GDK_KEY_Page_Down   GDK_Page_Down
#define GDK_KEY_Page_Up     GDK_Page_Up
#define GDK_KEY_plus        GDK_plus
#define GDK_KEY_Return      GDK_Return
#define GDK_KEY_Right       GDK_Right
#define GDK_KEY_space       GDK_space
#define GDK_KEY_Tab     GDK_Tab
#define GDK_KEY_Up      GDK_Up
#endif

#if !GTK_CHECK_VERSION(2,24,0)
#define gtk_combo_box_text_new gtk_combo_box_new_text
#define gtk_combo_box_text_append_text gtk_combo_box_append_text
#define gtk_combo_box_text_new_with_entry gtk_combo_box_entry_new_text
#define gtk_combo_box_new_with_model_and_entry(model) gtk_combo_box_entry_new_with_model(model, FILTER_COLUMN_FILTER)
#define GTK_COMBO_BOX_TEXT(combo) GTK_COMBO_BOX(combo)
#define gdk_window_get_width(window) compat_gdk_window_get_width(window)
#define gdk_window_get_height(window) compat_gdk_window_get_height(window)

gint compat_gdk_window_get_width(GdkWindow *window);
gint compat_gdk_window_get_height(GdkWindow *window);

#endif

#if !GTK_CHECK_VERSION(2,22,0)
#define gdk_window_create_similar_surface(window, content, width, height) compat_gdk_window_create_similar_surface(window, content, width, height)
cairo_surface_t *compat_gdk_window_create_similar_surface (GdkWindow *window, cairo_content_t content, gint width, gint height);

#define gdk_drag_context_get_selected_action(context) ((context)->action)
#define gdk_drag_context_get_suggested_action(context) ((context)->suggested_action)
#define gdk_drag_context_get_actions(context) ((context)->actions)
#endif

#endif /* COMPAT_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
