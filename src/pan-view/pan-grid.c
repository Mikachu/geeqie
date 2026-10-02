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

#include "pan-grid.h"

#include <math.h>

#include "pan-item.h"
#include "pan-util.h"
#include "pan-view-filter.h"

void pan_grid_compute(PanWindow *pw, FileData *dir_fd, gint *width, gint *height)
{
    GList *list = pan_list_tree(dir_fd, SORT_NAME, TRUE, pw->ignore_symlinks);
    pan_filter_fd_list(&list, pw->filter_ui->filter_elements);

    gint grid_size = (gint)sqrt((gdouble)g_list_length(list));
    if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
        grid_size = grid_size * (512 + pw->thumb_gap) * pw->image_size / 100;
    else
        grid_size = grid_size * (pw->thumb_size + pw->thumb_gap);

    gint next_y = 0;

    *width = PAN_BOX_BORDER * 2;
    *height = PAN_BOX_BORDER * 2;

    gint x = pw->thumb_gap;
    gint y = pw->thumb_gap;
    for (GList *work = list; work; work = work->next)
    {
        FileData *fd = work->data;
        PanItem *pi;

        if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
        {
            pi = pan_item_image_new(pw, fd, x, y, 10, 10);

            x += pi->width + pw->thumb_gap;

            if (y + pi->height + pw->thumb_gap > next_y)
                next_y = y + pi->height + pw->thumb_gap;

            if (x > grid_size)
            {
                x = pw->thumb_gap;
                y = next_y;
            }
        }
        else
        {
            pi = pan_item_thumb_new(pw, fd, x, y);

            x += pw->thumb_size + pw->thumb_gap;

            if (x > grid_size)
            {
                x = pw->thumb_gap;
                y += pw->thumb_size + pw->thumb_gap;
            }
        }
        pan_item_size_coordinates(pi, pw->thumb_gap, width, height);
    }
    g_list_free(list);
}
