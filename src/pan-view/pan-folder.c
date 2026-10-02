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

#include "pan-folder.h"

#include <math.h>

#include "pan-item.h"
#include "pan-util.h"
#include "pan-view-filter.h"

static void pan_flower_size(PanWindow *pw, gint *width, gint *height)
{
    gint x1 = 0, y1 = 0, x2 = 0, y2 = 0;

    for (GList *work = pw->list; work; work = work->next)
    {
        PanItem *pi = work->data;

        if (x1 > pi->x) x1 = pi->x;
        if (y1 > pi->y) y1 = pi->y;
        if (x2 < pi->x + pi->width)  x2 = pi->x + pi->width;
        if (y2 < pi->y + pi->height) y2 = pi->y + pi->height;
    }

    x1 -= PAN_BOX_BORDER;
    y1 -= PAN_BOX_BORDER;
    x2 += PAN_BOX_BORDER;
    y2 += PAN_BOX_BORDER;

    for (GList *work = pw->list; work; work = work->next)
    {
        PanItem *pi = work->data;

        pi->x -= x1;
        pi->y -= y1;

        if (pi->type == PAN_ITEM_TRIANGLE && pi->data)
            pan_item_tri_offset(pi, -x1, -y1);
    }

    if (width)  *width  = x2 - x1;
    if (height) *height = y2 - y1;
}

typedef struct FlowerGroup FlowerGroup;
struct FlowerGroup {
    GList *items;
    GList *children;
    gint x;
    gint y;
    gint width;
    gint height;

    gdouble angle;
    gint circumference;
    gint diameter;

    /* wedge layout: subtree bounding-circle diameter and child ring radius */
    gint span;
    gdouble ring;
    gboolean placed;
};

static void pan_flower_move(FlowerGroup *group, gint x, gint y)
{
    for (GList *work = group->items; work; work = work->next)
    {
        PanItem *pi = work->data;

        pi->x += x;
        pi->y += y;
    }

    group->x += x;
    group->y += y;
}

#define PI 3.14159265

static void pan_flower_position(FlowerGroup *group, FlowerGroup *parent,
                                gint *result_x, gint *result_y)
{
    gint x, y;
    gint radius;
    gdouble a;

    radius = parent->circumference / (2*PI);
    radius = MAX(radius, parent->diameter / 2 + group->diameter / 2);

    a = 2*PI * group->diameter / parent->circumference;

    x = (gint)((gdouble)radius * cos(parent->angle + a / 2));
    y = (gint)((gdouble)radius * sin(parent->angle + a / 2));

    parent->angle += a;

    x += parent->x;
    y += parent->y;

    x += parent->width  / 2;
    y += parent->height / 2;

    x -= group->width  / 2;
    y -= group->height / 2;

    *result_x = x;
    *result_y = y;
}

/* Wedge positioning with collision resolution: the wedge midpoint fixes
 * each child's angle (slices are proportional to whole-subtree span and
 * disjoint, so angular order is preserved), but the radius is only a
 * lower bound. Each child is pulled in as close to the parent as its
 * bounding circle allows, then pushed back out along its own ray if it
 * would overlap a sibling that is already placed. Children therefore
 * fill space across wedge boundaries radially instead of sitting on a
 * shared ring. */
static void pan_wedge_position(FlowerGroup *group, FlowerGroup *parent,
                               gint *result_x, gint *result_y)
{
    gdouble a = 2 * PI * group->span / parent->circumference;
    gdouble preferred = parent->angle + a / 2;
    gint n_siblings = 0;
    gdouble dist;

    parent->angle += a;

    dist = parent->diameter / 2.0 + group->span / 2.0 + PAN_BOX_BORDER;

    /* collect placed siblings */
    for (GList *work = parent->children; work; work = work->next)
    {
        FlowerGroup *s = work->data;
        if (s != group && s->placed)
            n_siblings++;
    }

    if (n_siblings == 0)
    {
        /* nobody placed: take the preferred ray at minimal distance */
        *result_x = (gint)(dist * cos(preferred)) + parent->x + parent->width  / 2 - group->width  / 2;
        *result_y = (gint)(dist * sin(preferred)) + parent->y + parent->height / 2 - group->height / 2;
        return;
    }

    typedef gdouble SiblingRel[3];
    gdouble (*sx)[3] = g_new(SiblingRel, n_siblings);   /* rel-x, rel-y, clearance */
    gint i = 0;
    for (GList *work = parent->children; work; work = work->next)
    {
        FlowerGroup *s = work->data;
        if (s == group || !s->placed) continue;

        sx[i][0] = s->x + s->width  / 2.0 - (parent->x + parent->width  / 2.0);
        sx[i][1] = s->y + s->height / 2.0 - (parent->y + parent->height / 2.0);
        sx[i][2] = (s->span + group->span) / 2.0 + PAN_BOX_BORDER;
        i++;
    }

    /* spiral search: for each ring, try evenly spaced angles starting
     * at the preferred one */
    gint found = FALSE;
    gdouble bx = 0, by = 0;

    for (gdouble d = dist; d < dist + parent->circumference && !found; d += PAN_BOX_BORDER)
    {
        for (gint k = 0; k < 24 && !found; k++)
        {
            gdouble th = preferred + k * (2 * PI / 24.0);
            gdouble cx = d * cos(th);
            gdouble cy = d * sin(th);
            gboolean free_ = TRUE;

            for (i = 0; i < n_siblings; i++)
            {
                gdouble dx = cx - sx[i][0];
                gdouble dy = cy - sx[i][1];
                if (dx * dx + dy * dy < sx[i][2] * sx[i][2])
                {
                    free_ = FALSE;
                    break;
                }
            }

            if (free_)
            {
                bx = cx;
                by = cy;
                found = TRUE;
            }
        }
    }

    g_free(sx);

    if (!found)
    {
        /* fall back to the preferred ray pushed way out */
        bx = (dist + parent->circumference) * cos(preferred);
        by = (dist + parent->circumference) * sin(preferred);
    }

    *result_x = (gint)bx + parent->x + parent->width  / 2 - group->width  / 2;
    *result_y = (gint)by + parent->y + parent->height / 2 - group->height / 2;
}

static void pan_flower_build(PanWindow *pw, FlowerGroup *group, FlowerGroup *parent, gboolean wedge)
{
    gint x, y;

    if (!group) return;

    if (parent && parent->children)
    {
        if (wedge)
            pan_wedge_position(group, parent, &x, &y);
        else
            pan_flower_position(group, parent, &x, &y);
    }
    else
    {
        x = 0;
        y = 0;
    }

    pan_flower_move(group, x, y);
    group->placed = TRUE;

    if (parent)
    {
        PanItem *pi;
        gint px, py, gx, gy;
        gint x1, y1, x2, y2;

        px = parent->x + parent->width  / 2;
        py = parent->y + parent->height / 2;

        gx = group->x + group->width  / 2;
        gy = group->y + group->height / 2;

        x1 = MIN(px, gx);
        y1 = MIN(py, gy);

        x2 = MAX(px, gx + 5);
        y2 = MAX(py, gy + 5);

        pi = pan_item_tri_new(pw, NULL, x1, y1, x2 - x1, y2 - y1,
                              px, py, gx, gy, gx + 5, gy + 5,
                              255, 40, 40, 128);
        pan_item_tri_border(pi, PAN_BORDER_1 | PAN_BORDER_3,
                            255, 0, 0, 128);
    }

    pw->list = g_list_concat(g_steal_pointer(&group->items),
                             pw->list);
    group->circumference = 0;

    GList *last = NULL;
    for (GList *work = group->children; work; work = work->next)
    {
        FlowerGroup *child = work->data;

        group->circumference += wedge ? child->span : child->diameter;
        last = work;
    }

    for (GList *work = last; work; work = work->prev)
    {
        FlowerGroup *child = work->data;

        pan_flower_build(pw, child, group, wedge);
    }
    /* wedge: now that every child has a final position, tighten this
     * group's bounding circle to the real extents. Siblings placed after
     * this group can then trust group->span and only need to be tested
     * against it — not against the whole subtree. */
    if (wedge && group->children)
    {
        gdouble px = group->x + group->width  / 2.0;
        gdouble py = group->y + group->height / 2.0;

        group->span = group->diameter;
        for (GList *work = group->children; work; work = work->next)
        {
            FlowerGroup *child = work->data;
            gdouble dx = child->x + child->width  / 2.0 - px;
            gdouble dy = child->y + child->height / 2.0 - py;
            gdouble reach = sqrt(dx * dx + dy * dy) + child->span / 2.0;

            if (2 * reach > group->span)
                group->span = (gint)(2 * reach + 0.5);
        }
    }

    g_clear_list(&group->children, g_free);
}

static FlowerGroup *pan_flower_group(PanWindow *pw, FileData *dir_fd, gint x, gint y)
{
    FlowerGroup *group;
    GList *f, *d;
    PanItem *pi_box;
    gint x_start;
    gint y_height;
    gint grid_size;
    gint grid_count;

    if (!filelist_read(dir_fd, &f, &d)) return NULL;
    if (!f && !d) return NULL;

    f = filelist_sort(f, SORT_NAME, TRUE);
    d = filelist_sort(d, SORT_NAME, TRUE);

    pan_filter_fd_list(&f, pw->filter_ui->filter_elements);

    PanItem *pi_label = pan_item_text_new(pw, x, y, g_strdup(dir_fd->name), PAN_TEXT_ATTR_NONE,
                                          PAN_TEXT_BORDER_SIZE,
                                          PAN_TEXT_COLOR, 255);

    y += pi_label->height;

    pi_box = pan_item_box_new(pw, file_data_ref(dir_fd),
                              x, y,
                              PAN_BOX_BORDER * 2, PAN_BOX_BORDER * 2,
                              PAN_BOX_OUTLINE_THICKNESS,
                              PAN_BOX_COLOR, PAN_BOX_ALPHA,
                              PAN_BOX_OUTLINE_COLOR, PAN_BOX_OUTLINE_ALPHA);

    x += PAN_BOX_BORDER;
    y += PAN_BOX_BORDER;

    grid_size = (gint)(sqrt(g_list_length(f)) + 0.9);
    grid_count = 0;
    x_start = x;
    y_height = y;

    for (GList *work = f; work; work = work->next)
    {
        FileData *fd = work->data;
        PanItem *pi;

        if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
        {
            pi = pan_item_image_new(pw, fd, x, y, 10, 10);
            x += pi->width + pw->thumb_gap;
            if (pi->height > y_height) y_height = pi->height;
        }
        else
        {
            pi = pan_item_thumb_new(pw, fd, x, y);
            x += pw->thumb_size + pw->thumb_gap;
            y_height = pw->thumb_size;
        }

        grid_count++;
        if (grid_count >= grid_size)
        {
            grid_count = 0;
            x = x_start;
            y += y_height + pw->thumb_gap;
            y_height = 0;
        }

        pan_item_size_by_item(pi_box, pi, PAN_BOX_BORDER);
    }

    group = g_new0(FlowerGroup, 1);

    if (pi_label->width > pi_box->width)
    {
        gint align = (pi_label->width - pi_box->width) / 2;
        pi_label->x -= align;
        group->x -= align;
    }

    group->items = g_steal_pointer(&pw->list);

    group->width  = MAX(pi_box->width, pi_label->width);
    group->height = pi_box->y + pi_box->height;
    group->diameter = (gint)sqrt(group->width  * group->width +
                                 group->height * group->height);

    group->children = NULL;

    for (GList *work = d; work; work = work->next)
    {
        FileData *fd = work->data;

        if (!pan_is_ignored(fd->path, pw->ignore_symlinks))
        {
            FlowerGroup *child = pan_flower_group(pw, fd, 0, 0);
            if (child)
                group->children = g_list_prepend(group->children, child);
        }
    }

    /* subtree extent for the wedge layout */
    group->span = group->diameter;
    group->ring = 0;

    if (group->children)
    {
        gint total = 0;
        gint max_span = 0;
        gdouble r;

        for (GList *work = group->children; work; work = work->next)
        {
            FlowerGroup *child = work->data;
            total += child->span;
            max_span = MAX(max_span, child->span);
        }

        /* each child occupies angle 2*PI*span/total on the ring; solve
         * the radius so adjacent bounding circles don't overlap, and
         * so children clear this group's own bubble */
        r = total / (2 * PI);
        if (group->children->next)
            for (GList *work = group->children; work; work = work->next)
            {
                FlowerGroup *child = work->data;
                FlowerGroup *next = work->next ? work->next->data
                                               : group->children->data;
                gdouble need = (child->span + next->span) /
                               (4 * sin(PI * (child->span + next->span) / (2 * total)));

                if (need > r)
                    r = need;
            }
        r = MAX(r, group->diameter / 2.0 + max_span / 2.0 + PAN_BOX_BORDER);

        group->ring = r;
        group->span = MAX(group->span, (gint)(2 * r + max_span));
    }

    if (!f && !group->children)
    {
        g_clear_list(&group->items, (GDestroyNotify)pan_item_free);
        g_clear_pointer(&group, g_free);
    }

    g_list_free(f);
    filelist_free(d);

    return group;
}

void pan_radial_compute(PanWindow *pw, FileData *dir_fd,
                        gint *width, gint *height,
                        gint *scroll_x, gint *scroll_y, gboolean wedge)
{
    FlowerGroup *group = pan_flower_group(pw, dir_fd, 0, 0);
    pan_flower_build(pw, group, NULL, wedge);
    g_clear_pointer(&group, g_free);

    /* reorder: images/thumbs on top, connectors under them, rest below */
    GList *top = NULL, *tri = NULL, *rest = NULL;
    for (GList *work = pw->list; work; work = work->next)
    {
        PanItem *pi = work->data;
        GList **dst;

        if (pi->type == PAN_ITEM_IMAGE ||
            pi->type == PAN_ITEM_THUMB ||
            pi->type == PAN_ITEM_BOX ||
            pi->type == PAN_ITEM_TEXT)
            dst = &top;
        else if (pi->type == PAN_ITEM_TRIANGLE)
            dst = &tri;
        else
            dst = &rest;

        *dst = g_list_prepend(*dst, pi);
    }
    g_list_free(pw->list);
    pw->list = g_list_reverse(g_list_concat(rest, g_list_concat(tri, top)));

    pan_flower_size(pw, width, height);

    GList *list = pan_item_find_by_fd(pw, PAN_ITEM_BOX, dir_fd, FALSE, FALSE);
    if (list)
    {
        PanItem *pi = list->data;
        *scroll_x = pi->x + pi->width / 2;
        *scroll_y = pi->y + pi->height / 2;
    }
    g_list_free(list);
}

void pan_flower_compute(PanWindow *pw, FileData *dir_fd,
                        gint *width, gint *height,
                        gint *scroll_x, gint *scroll_y)
{
    pan_radial_compute(pw, dir_fd, width, height, scroll_x, scroll_y, FALSE);
}

void pan_wedge_compute(PanWindow *pw, FileData *dir_fd,
                       gint *width, gint *height,
                       gint *scroll_x, gint *scroll_y)
{
    pan_radial_compute(pw, dir_fd, width, height, scroll_x, scroll_y, TRUE);
}

static void pan_folder_tree_path(PanWindow *pw, FileData *dir_fd,
                                 gint *x, gint *y, gint *level,
                                 PanItem *parent,
                                 gint *width, gint *height)
{
    GList *f, *d;
    PanItem *pi_box;
    gint y_height = 0;

    if (!filelist_read(dir_fd, &f, &d)) return;
    if (!f && !d) return;

    f = filelist_sort(f, SORT_NAME, TRUE);
    d = filelist_sort(d, SORT_NAME, TRUE);

    pan_filter_fd_list(&f, pw->filter_ui->filter_elements);

    *x = PAN_BOX_BORDER + ((*level) * MAX(PAN_BOX_BORDER, pw->thumb_gap));

    pi_box = pan_item_text_new(pw, *x, *y, g_strdup(dir_fd->name), PAN_TEXT_ATTR_NONE,
                               PAN_TEXT_BORDER_SIZE,
                               PAN_TEXT_COLOR, 255);

    *y += pi_box->height;

    pi_box = pan_item_box_new(pw, file_data_ref(dir_fd),
                              *x, *y,
                              PAN_BOX_BORDER, PAN_BOX_BORDER,
                              PAN_BOX_OUTLINE_THICKNESS,
                              PAN_BOX_COLOR, PAN_BOX_ALPHA,
                              PAN_BOX_OUTLINE_COLOR, PAN_BOX_OUTLINE_ALPHA);

    *x += PAN_BOX_BORDER;
    *y += PAN_BOX_BORDER;

    for (GList *work = f; work; work = work->next)
    {
        FileData *fd = work->data;
        PanItem *pi;

        if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
        {
            pi = pan_item_image_new(pw, fd, *x, *y, 10, 10);
            *x += pi->width + pw->thumb_gap;
            if (pi->height > y_height) y_height = pi->height;
        }
        else
        {
            pi = pan_item_thumb_new(pw, fd, *x, *y);
            *x += pw->thumb_size + pw->thumb_gap;
            y_height = pw->thumb_size;
        }

        pan_item_size_by_item(pi_box, pi, PAN_BOX_BORDER);
    }

    if (f)
    {
        *y = pi_box->y + pi_box->height;
        g_list_free(f);
    }

    for (GList *work = d; work; work = work->next)
    {
        FileData *fd = work->data;

        if (!pan_is_ignored(fd->path, pw->ignore_symlinks))
        {
            *level = *level + 1;
            pan_folder_tree_path(pw, fd, x, y, level, pi_box, width, height);
            *level = *level - 1;
        }
    }

    filelist_free(d);

    pan_item_size_by_item(parent, pi_box, PAN_BOX_BORDER);

    if (*y < pi_box->y + pi_box->height + PAN_BOX_BORDER)
        *y = pi_box->y + pi_box->height + PAN_BOX_BORDER;

    pan_item_size_coordinates(pi_box, PAN_BOX_BORDER, width, height);
}

void pan_folder_tree_compute(PanWindow *pw, FileData *dir_fd, gint *width, gint *height)
{
    gint x, y;
    gint level;
    gint w, h;

    level = 0;
    x = PAN_BOX_BORDER;
    y = PAN_BOX_BORDER;
    w = PAN_BOX_BORDER * 2;
    h = PAN_BOX_BORDER * 2;

    pan_folder_tree_path(pw, dir_fd, &x, &y, &level, NULL, &w, &h);

    if (width) *width = w;
    if (height) *height = h;
}
