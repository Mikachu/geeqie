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

#include "main.h"
#include "compat.h"

#if !GTK_CHECK_VERSION(2,24,0)
gint compat_gdk_window_get_width(GdkWindow *window)
{
    gint w, h;
    gdk_drawable_get_size(window, &w, &h);
    return w;
}

gint compat_gdk_window_get_height(GdkWindow *window)
{
    gint w, h;
    gdk_drawable_get_size(window, &w, &h);
    return h;
}
#endif

#if !GTK_CHECK_VERSION(2,22,0)
cairo_surface_t *compat_gdk_window_create_similar_surface (GdkWindow *window, cairo_content_t content, gint width, gint height)
{
    cairo_t *cr = gdk_cairo_create(window);
    cairo_surface_t *ws = cairo_get_target(cr);
    cairo_surface_t *ret = cairo_surface_create_similar(ws, content, width, height);
    cairo_destroy(cr);
    return ret;
}
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
