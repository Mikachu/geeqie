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

#ifndef TYPEDEFS_H
#define TYPEDEFS_H

typedef enum {
    MOUSE_BUTTON_LEFT        = 1,
    MOUSE_BUTTON_MIDDLE      = 2,
    MOUSE_BUTTON_RIGHT       = 3,
    MOUSE_BUTTON_WHEEL_UP    = 4,
    MOUSE_BUTTON_WHEEL_DOWN  = 5,
    MOUSE_BUTTON_WHEEL_LEFT  = 6,
    MOUSE_BUTTON_WHEEL_RIGHT = 7,
    MOUSE_BUTTON_BACK        = 8,
    MOUSE_BUTTON_FORWARD     = 9,
} MouseButton;

#define CMD_COPY     "geeqie-copy-command.desktop"
#define CMD_MOVE     "geeqie-move-command.desktop"
#define CMD_RENAME   "geeqie-rename-command.desktop"
#define CMD_DELETE   "geeqie-delete-command.desktop"
#define CMD_FOLDER   "geeqie-folder-command.desktop"

typedef enum {
    SORT_NONE,
    SORT_NAME,
    SORT_SIZE,
    SORT_TIME,
    SORT_CTIME,
    SORT_PATH,
    SORT_NUMBER,
    SORT_EXIFTIME
} SortType;

struct ThumbLoader;
typedef void (*ThumbLoaderFunc)(struct ThumbLoader *tl, gpointer data);

typedef void (*FileUtilDoneFunc)(gboolean success, const gchar *done_path, gpointer data);

struct ImageWindow;
typedef gint (*ImageTileRequestFunc)(struct ImageWindow *imd, gint x, gint y,
                                     gint width, gint height, GdkPixbuf *pixbuf, gpointer);
typedef void (*ImageTileDisposeFunc)(struct ImageWindow *imd, gint x, gint y,
                                     gint width, gint height, GdkPixbuf *pixbuf, gpointer);

#endif
