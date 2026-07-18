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

#ifndef UI_SPINNER_H
#define UI_SPINNER_H


#define SPINNER_SPEED 100


extern const guint8 icon_spinner[];
extern const guint8 icon_tabcomp[];

/* if path is NULL, the built in spinner is used,
 * otherwise path must be the location of the first image of the
 * spinner without the 00.png portion of the pathname, example:
 *
 *     /path/to/spinnerimg_
 *
 * the files required are then:
 *
 *     /path/to/spinnerimg_00.png   non-animated state
 *     /path/to/spinnerimg_01.png   animation frame 1
 *     /path/to/spinnerimg_02.png   animation frame 2
 *     [continues to last frame...]
 */
GtkWidget *spinner_new(const gchar *path, gint interval);

void spinner_set_interval(GtkWidget *spinner, gint interval);
void spinner_step(GtkWidget *spinner, gboolean reset);


#endif
