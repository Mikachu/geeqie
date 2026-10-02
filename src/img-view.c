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

#include "main.h"
#include "img-view.h"

#include "collect.h"
#include "collect-io.h"
#include "dnd.h"
#include "editors.h"
#include "filedata.h"
#include "fullscreen.h"
#include "image.h"
#include "image-load.h"
#include "image-overlay.h"
#include "layout.h"
#include "layout_image.h"
#include "menu.h"
#include "misc.h"
#include "pixbuf_util.h"
#include "pixbuf-renderer.h"
#include "preferences.h"
#include "print.h"
#include "slideshow.h"
#include "ui_fileops.h"
#include "ui_menu.h"
#include "uri_utils.h"
#include "utilops.h"
#include "window.h"

#include <gdk/gdkkeysyms.h> /* for keyboard values */


typedef struct ViewWindow ViewWindow;
struct ViewWindow
{
    GtkWidget *window;
    ImageWindow *imd;
    FullScreenData *fs;
    SlideShowData *ss;

    GList *list;
    GList *list_pointer; /* currently displayed image */
};


static GList *view_window_list = NULL;


static GtkWidget *view_popup_menu(ViewWindow *vw);
static void view_fullscreen_toggle(ViewWindow *vw, gboolean force_off);
static void view_overlay_toggle(ViewWindow *vw);

static void view_slideshow_next(ViewWindow *vw);
static void view_slideshow_prev(ViewWindow *vw);
static void view_slideshow_start(ViewWindow *vw);
static void view_slideshow_stop(ViewWindow *vw);

static void view_window_close(ViewWindow *vw);

static void view_window_dnd_init(ViewWindow *vw);

static void view_window_notify_cb(FileData *fd, NotifyType type, gpointer data);

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

static ImageWindow *view_window_active_image(ViewWindow *vw)
{
    return vw->fs ? vw->fs->imd : vw->imd;
}

static void view_window_set_list(ViewWindow *vw, GList *list)
{
    g_clear_pointer(&vw->list, filelist_free);
    vw->list_pointer = NULL;

    vw->list = filelist_copy(list);
}

static gboolean view_window_contains_collection(ViewWindow *vw)
{
    CollectionData *cd;
    CollectInfo *info;

    cd = image_get_collection(view_window_active_image(vw), &info);

    return (cd && info);
}

static void view_collection_step(ViewWindow *vw, gboolean next)
{
    ImageWindow *imd = view_window_active_image(vw);
    CollectionData *cd;
    CollectInfo *info;
    CollectInfo *read_ahead_info = NULL;

    cd = image_get_collection(imd, &info);

    if (!cd || !info) return;

    if (next)
    {
        /* XXX each of these collection_next/prev_by_info does a linear scan
         * through the collection's list to find info */
        info = collection_next_by_info(cd, info);
        if (options->image.enable_read_ahead)
        {
            read_ahead_info = collection_next_by_info(cd, info);
            if (!read_ahead_info)
                read_ahead_info = collection_prev_by_info(cd, info);
        }
    }
    else
    {
        info = collection_prev_by_info(cd, info);
        if (options->image.enable_read_ahead)
        {
            read_ahead_info = collection_prev_by_info(cd, info);
            if (!read_ahead_info)
                read_ahead_info = collection_next_by_info(cd, info);
        }
    }

    if (info)
    {
        image_change_from_collection(imd, cd, info, image_zoom_get_default(imd));

        if (read_ahead_info)
            image_prebuffer_set(imd, read_ahead_info->fd);
    }

}

static void view_collection_step_to_end(ViewWindow *vw, gboolean last)
{
    ImageWindow *imd = view_window_active_image(vw);
    CollectionData *cd;
    CollectInfo *info;
    CollectInfo *read_ahead_info = NULL;

    cd = image_get_collection(imd, &info);

    if (!cd || !info) return;

    if (last)
        info = collection_get_last(cd);
    else
        info = collection_get_first(cd);

    if (options->image.enable_read_ahead)
        read_ahead_info = last ? collection_prev_by_info(cd, info)
                               : collection_next_by_info(cd, info);

    if (info)
    {
        image_change_from_collection(imd, cd, info, image_zoom_get_default(imd));
        if (read_ahead_info)
            image_prebuffer_set(imd, read_ahead_info->fd);
    }
}

static void view_list_step(ViewWindow *vw, gboolean next)
{
    ImageWindow *imd = view_window_active_image(vw);
    FileData *fd;
    GList *image, *image_readahead = NULL;

    if (!vw->list) return;

    image = vw->list_pointer;
    if (!image) return;

    if (next)
    {
        image = image->next;
        if (image) image_readahead = image->next;
    }
    else
    {
        image = image->prev;
        if (image) image_readahead = image->prev;
    }

    if (!image) return;

    vw->list_pointer = image;
    fd = image->data;
    image_change_fd(imd, fd, image_zoom_get_default(imd));

    if (options->image.enable_read_ahead && image_readahead)
    {
        FileData *next_fd = image_readahead->data;
        image_prebuffer_set(imd, next_fd);
    }
}

static void view_list_step_to_end(ViewWindow *vw, gboolean last)
{
    ImageWindow *imd = view_window_active_image(vw);
    FileData *fd;
    GList *image;
    GList *image_readahead;

    if (!vw->list) return;

    if (last)
    {
        image = g_list_last(vw->list);
        image_readahead = image->prev;
    }
    else
    {
        image = vw->list;
        image_readahead = image->next;
    }

    vw->list_pointer = image;
    fd = image->data;
    image_change_fd(imd, fd, image_zoom_get_default(imd));

    if (options->image.enable_read_ahead && image_readahead)
    {
        FileData *next_fd = image_readahead->data;
        image_prebuffer_set(imd, next_fd);
    }
}

static void view_step_next(ViewWindow *vw)
{
    if (vw->ss)
        view_slideshow_next(vw);
    else if (vw->list)
        view_list_step(vw, TRUE);
    else
        view_collection_step(vw, TRUE);
}

static void view_step_prev(ViewWindow *vw)
{
    if (vw->ss)
        view_slideshow_prev(vw);
    else if (vw->list)
        view_list_step(vw, FALSE);
    else
        view_collection_step(vw, FALSE);
}

static void view_step_to_end(ViewWindow *vw, gboolean last)
{
    if (vw->list)
        view_list_step_to_end(vw, last);
    else
        view_collection_step_to_end(vw, last);
}

/*
 *-----------------------------------------------------------------------------
 * view window accelerators
 *
 * Bound to the same accel paths as the layout window actions, so both
 * default keys and user remapping are shared.
 * Accel closures connected via gtk_accel_group_connect_by_path() are
 * invoked with the accelerator signature (group, acceleratable, keyval,
 * mod, user_data), NOT the (widget, data) convention used by menu items.
 * vw arrives in data (the user_data passed to g_cclosure_new);
 *-----------------------------------------------------------------------------
 */

static void view_accel_step_prev_cb(GtkAccelGroup *group, GObject *obj,
                                    guint keyval, GdkModifierType mod,
                                    gpointer data)
{
    view_step_prev(data);
}

static void view_accel_step_next_cb(GtkAccelGroup *group, GObject *obj,
                                    guint keyval, GdkModifierType mod,
                                    gpointer data)
{
    view_step_next(data);
}

static void view_accel_step_first_cb(GtkAccelGroup *group, GObject *obj,
                                     guint keyval, GdkModifierType mod,
                                     gpointer data)
{
    view_step_to_end(data, FALSE);
}

static void view_accel_step_last_cb(GtkAccelGroup *group, GObject *obj,
                                    guint keyval, GdkModifierType mod,
                                    gpointer data)
{
    view_step_to_end(data, TRUE);
}

static void view_accel_slideshow_toggle_cb(GtkAccelGroup *group, GObject *obj,
                                           guint keyval, GdkModifierType mod,
                                           gpointer data)
{
    ViewWindow *vw = data;

    if (vw->ss)
        view_slideshow_stop(vw);
    else
        view_slideshow_start(vw);
}

static void view_accel_reload_cb(GtkAccelGroup *group, GObject *obj,
                                 guint keyval, GdkModifierType mod,
                                 gpointer data)
{
    image_reload(view_window_active_image(data));
}

static void view_accel_overlay_cb(GtkAccelGroup *group, GObject *obj,
                                  guint keyval, GdkModifierType mod,
                                  gpointer data)
{
    view_overlay_toggle(data);
}

static void view_accel_escape_cb(GtkAccelGroup *group, GObject *obj,
                                 guint keyval, GdkModifierType mod,
                                 gpointer data)
{
    ViewWindow *vw = data;

    if (vw->fs)
        view_fullscreen_toggle(vw, TRUE);
}

static void view_accel_print_cb(GtkAccelGroup *group, GObject *obj,
                                guint keyval, GdkModifierType mod,
                                gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;
    FileData *fd;

    view_fullscreen_toggle(vw, TRUE);
    imd = view_window_active_image(vw);
    fd = image_get_fd(imd);
    print_window_new(fd,
                     fd ? g_list_append(NULL, file_data_ref(fd)) : NULL,
                     filelist_copy(vw->list), vw->window);
}

static void view_accel_config_cb(GtkAccelGroup *group, GObject *obj,
                                 guint keyval, GdkModifierType mod,
                                 gpointer data)
{
    show_config_window();
}

#define VIEW_ACCEL_ZOOM_CB(name, zoom)                                   \
    static void name(GtkAccelGroup *group, GObject *obj,                 \
                     guint keyval, GdkModifierType mod,                  \
                     gpointer data)                                      \
    {                                                                    \
        image_zoom_set(view_window_active_image(data), zoom);            \
    }

VIEW_ACCEL_ZOOM_CB(view_accel_zoom_2_1_cb,  2.0)
VIEW_ACCEL_ZOOM_CB(view_accel_zoom_3_1_cb,  3.0)
VIEW_ACCEL_ZOOM_CB(view_accel_zoom_4_1_cb,  4.0)
VIEW_ACCEL_ZOOM_CB(view_accel_zoom_1_2_cb, -2.0)
VIEW_ACCEL_ZOOM_CB(view_accel_zoom_1_3_cb, -3.0)
VIEW_ACCEL_ZOOM_CB(view_accel_zoom_1_4_cb, -4.0)

static void view_accel_fill_vert_cb(GtkAccelGroup *group, GObject *obj,
                                    guint keyval, GdkModifierType mod,
                                    gpointer data)
{
    image_zoom_set_fill_geometry(view_window_active_image(data), TRUE);
}

static void view_accel_fill_horz_cb(GtkAccelGroup *group, GObject *obj,
                                    guint keyval, GdkModifierType mod,
                                    gpointer data)
{
    image_zoom_set_fill_geometry(view_window_active_image(data), FALSE);
}

static void view_accel_rotate_cw_cb(GtkAccelGroup *group, GObject *obj,
                                    guint keyval, GdkModifierType mod,
                                    gpointer data)
{
    image_alter_orientation(view_window_active_image(data), ALTER_ROTATE_90);
}

static void view_accel_rotate_ccw_cb(GtkAccelGroup *group, GObject *obj,
                                     guint keyval, GdkModifierType mod,
                                     gpointer data)
{
    image_alter_orientation(view_window_active_image(data), ALTER_ROTATE_90_CC);
}

static void view_accel_rotate_180_cb(GtkAccelGroup *group, GObject *obj,
                                     guint keyval, GdkModifierType mod,
                                     gpointer data)
{
    image_alter_orientation(view_window_active_image(data), ALTER_ROTATE_180);
}

static void view_accel_mirror_cb(GtkAccelGroup *group, GObject *obj,
                                 guint keyval, GdkModifierType mod,
                                 gpointer data)
{
    image_alter_orientation(view_window_active_image(data), ALTER_MIRROR);
}

static void view_accel_flip_cb(GtkAccelGroup *group, GObject *obj,
                               guint keyval, GdkModifierType mod,
                               gpointer data)
{
    image_alter_orientation(view_window_active_image(data), ALTER_FLIP);
}

static void view_accel_desaturate_cb(GtkAccelGroup *group, GObject *obj,
                                     guint keyval, GdkModifierType mod,
                                     gpointer data)
{
    ImageWindow *imd = view_window_active_image(data);

    image_set_desaturate(imd, !image_get_desaturate(imd));
}

/*
 *-----------------------------------------------------------------------------
 * view window keyboard
 *-----------------------------------------------------------------------------
 */

static void view_window_menu_pos_cb(GtkMenu *menu, gint *x, gint *y,
                                    gboolean *push_in, gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;

    imd = view_window_active_image(vw);
    gdk_window_get_origin(gtk_widget_get_window(imd->pr), x, y);
    popup_menu_position_clamp(menu, x, y, 0);
}

static gboolean view_window_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;
    gboolean arrow_handled = TRUE;
    GtkWidget *menu;
    gint x = 0;
    gint y = 0;

    imd = view_window_active_image(vw);

    switch (event->keyval)
    {
        case GDK_KEY_Left: case GDK_KEY_KP_Left:
            x -= 1;
            break;
        case GDK_KEY_Right: case GDK_KEY_KP_Right:
            x += 1;
            break;
        case GDK_KEY_Up: case GDK_KEY_KP_Up:
            y -= 1;
            break;
        case GDK_KEY_Down: case GDK_KEY_KP_Down:
            y += 1;
            break;
        default:
            arrow_handled = FALSE;
            break;
    }

    if (x != 0 || y != 0)
    {
        if (event->state & GDK_SHIFT_MASK)
        {
            x *= 3;
            y *= 3;
        }

        keyboard_scroll_calc(&x, &y, event);
        image_scroll(imd, x, y);
    }

    if (arrow_handled) return TRUE;

    if (!(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK)))
    {
        switch (event->keyval)
        {
            case GDK_KEY_Menu:
            case GDK_KEY_F10:
                menu = view_popup_menu(vw);
                gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
                               view_window_menu_pos_cb, vw, 0, event->time);
                return TRUE;
            default:
                return FALSE;
        }
    }

    return FALSE;
}

/*
 *-----------------------------------------------------------------------------
 * view window main routines
 *-----------------------------------------------------------------------------
 */

static gboolean mouse_binding_activate(ViewWindow *vw,
                                       guint button, GdkModifierType state);
static void button_cb(ImageWindow *imd, GdkEventButton *event, gpointer data)
{
    ViewWindow *vw = data;
    GtkWidget *menu;

    if (mouse_binding_activate(vw, event->button, event->state)) return;

    switch (event->button)
    {
        case MOUSE_BUTTON_LEFT:
            if (options->image_lm_click_nav)
                view_step_next(vw);
            break;
        case MOUSE_BUTTON_MIDDLE:
            if (options->image_lm_click_nav)
                view_step_prev(vw);
            break;
        case MOUSE_BUTTON_RIGHT:
            menu = view_popup_menu(vw);
            gtk_menu_popup(GTK_MENU(menu), NULL, NULL, popup_menu_at_event,
                           event, event->button, event->time);
            break;
        default:
            break;
    }
}

static guint scroll_direction_to_button(GdkScrollDirection direction)
{
    switch (direction)
    {
        case GDK_SCROLL_UP:    return MOUSE_BUTTON_WHEEL_UP;
        case GDK_SCROLL_DOWN:  return MOUSE_BUTTON_WHEEL_DOWN;
        case GDK_SCROLL_LEFT:  return MOUSE_BUTTON_WHEEL_LEFT;
        case GDK_SCROLL_RIGHT: return MOUSE_BUTTON_WHEEL_RIGHT;
        default:
            return 0;
    }
}

static void scroll_cb(ImageWindow *imd, GdkEventScroll *event, gpointer data)
{
    ViewWindow *vw = data;

    guint button = scroll_direction_to_button(event->direction);
    if (mouse_binding_activate(vw, button, event->state))
        return;

    if (event->state & GDK_CONTROL_MASK)
    {
        switch (event->direction)
        {
            case GDK_SCROLL_UP:
                image_zoom_adjust_at_point(imd, get_zoom_increment(), event->x, event->y);
                break;
            case GDK_SCROLL_DOWN:
                image_zoom_adjust_at_point(imd, -get_zoom_increment(), event->x, event->y);
                break;
            default:
                break;
        }
    }
    else if ((event->state & GDK_SHIFT_MASK) != (guint)(options->mousewheel_scrolls))
    {
        switch (event->direction)
        {
            case GDK_SCROLL_UP:    image_scroll(imd, 0, -MOUSEWHEEL_SCROLL_SIZE); break;
            case GDK_SCROLL_DOWN:  image_scroll(imd, 0,  MOUSEWHEEL_SCROLL_SIZE); break;
            case GDK_SCROLL_LEFT:  image_scroll(imd, -MOUSEWHEEL_SCROLL_SIZE, 0); break;
            case GDK_SCROLL_RIGHT: image_scroll(imd,  MOUSEWHEEL_SCROLL_SIZE, 0); break;
            default:
                break;
        }
    }
    else
    {
        switch (event->direction)
        {
            case GDK_SCROLL_UP:   view_step_prev(vw); break;
            case GDK_SCROLL_DOWN: view_step_next(vw); break;
            default:
                break;
        }
    }
}

static void view_image_set_buttons(ViewWindow *vw, ImageWindow *imd)
{
    image_set_button_func(imd, button_cb, vw);
    image_set_scroll_func(imd, scroll_cb, vw);
}

static void view_fullscreen_stop_func(FullScreenData *fs, gpointer data)
{
    ViewWindow *vw = data;

    vw->fs = NULL;

    if (vw->ss) vw->ss->imd = vw->imd;
}

static void view_fullscreen_toggle(ViewWindow *vw, gboolean force_off)
{
    if (force_off && !vw->fs) return;

    if (vw->fs)
        fullscreen_stop(vw->fs);
    else
        vw->fs = fullscreen_start(vw->window, vw->imd, vw->window,
                                  FALSE, view_fullscreen_stop_func, vw);
}

static void view_overlay_toggle(ViewWindow *vw)
{
    ImageWindow *imd = view_window_active_image(vw);

    image_osd_toggle(imd);
}

static void view_slideshow_next(ViewWindow *vw)
{
    if (vw->ss)
        slideshow_next(vw->ss);
}

static void view_slideshow_prev(ViewWindow *vw)
{
    if (vw->ss)
        slideshow_prev(vw->ss);
}

static void view_slideshow_stop_func(SlideShowData *fs, gpointer data)
{
    ViewWindow *vw = data;

    vw->ss = NULL;

    FileData *fd = image_get_fd(view_window_active_image(vw));
    GList *work = g_list_find(vw->list, fd);
    if (work) /* XXX why not just keep this in sync during the slideshow? */
        vw->list_pointer = work;
}

static void view_slideshow_start(ViewWindow *vw)
{
    if (!vw->ss)
    {
        CollectionData *cd;
        CollectInfo *info;

        if (vw->list)
        {
            vw->ss = slideshow_start_from_filelist(NULL, view_window_active_image(vw),
                                                   filelist_copy(vw->list),
                                                   view_slideshow_stop_func, vw);
            vw->list_pointer = NULL;
            return;
        }

        cd = image_get_collection(view_window_active_image(vw), &info);
        if (cd && info)
        {
            vw->ss = slideshow_start_from_collection(NULL, view_window_active_image(vw), cd,
                                                     view_slideshow_stop_func, vw, info);
        }
    }
}

static void view_slideshow_stop(ViewWindow *vw)
{
    if (vw->ss)
        slideshow_free(vw->ss);
}

static void view_window_destroy_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;

    view_window_list = g_list_remove(view_window_list, vw);

    view_slideshow_stop(vw);
    fullscreen_stop(vw->fs);

    filelist_free(vw->list);

    file_data_unregister_notify_func(view_window_notify_cb, vw);

    g_free(vw);
}

static void view_window_close(ViewWindow *vw)
{
    view_slideshow_stop(vw);
    view_fullscreen_toggle(vw, TRUE);
    gtk_widget_destroy(vw->window);
}

static gboolean view_window_delete_cb(GtkWidget *w, GdkEventAny *event, gpointer data)
{
    ViewWindow *vw = data;

    view_window_close(vw);
    return TRUE;
}

static void view_window_accels_init(ViewWindow *vw);

static ViewWindow *real_view_window_new(FileData *fd, GList *list,
                                        CollectionData *cd, CollectInfo *info)
{
    ViewWindow *vw;
    GtkAllocation req_size;
    GdkGeometry geometry;
    gint w, h;

    if (!fd && !list && (!cd || !info)) return NULL;

    vw = g_new0(ViewWindow, 1);

    vw->window = window_new(GTK_WINDOW_TOPLEVEL, "view", PIXBUF_INLINE_ICON_VIEW, NULL, NULL);

    geometry.min_width = DEFAULT_MINIMAL_WINDOW_SIZE;
    geometry.min_height = DEFAULT_MINIMAL_WINDOW_SIZE;
    gtk_window_set_geometry_hints(GTK_WINDOW(vw->window), NULL, &geometry, GDK_HINT_MIN_SIZE);

    gtk_window_set_resizable(GTK_WINDOW(vw->window), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(vw->window), 0);

    vw->imd = image_new(FALSE);
    image_color_profile_set(vw->imd,
                            options->color_profile.input_type,
                            options->color_profile.use_image);
    image_color_profile_set_use(vw->imd, options->color_profile.enabled);

    image_background_set_color_from_options(vw->imd, FALSE);

    image_attach_window(vw->imd, vw->window, NULL, GQ_APPNAME, TRUE);

    image_top_window_set_sync(vw->imd, TRUE);

    gtk_container_add(GTK_CONTAINER(vw->window), vw->imd->widget);
    gtk_widget_show(vw->imd->widget);

    view_window_dnd_init(vw);

    view_image_set_buttons(vw, vw->imd);

    g_signal_connect(G_OBJECT(vw->window), "destroy",
                     G_CALLBACK(view_window_destroy_cb), vw);
    g_signal_connect(G_OBJECT(vw->window), "delete_event",
                     G_CALLBACK(view_window_delete_cb), vw);
    g_signal_connect(G_OBJECT(vw->window), "key_press_event",
                     G_CALLBACK(view_window_key_press_cb), vw);

    view_window_accels_init(vw);

    if (cd && info)
    {
        image_change_from_collection(vw->imd, cd, info, image_zoom_get_default(NULL));
        /* Grab the fd so we can correctly size the window in
           the call to image_load_dimensions() below. */
        fd = info->fd;
        if (options->image.enable_read_ahead)
        {
            CollectInfo *r_info = collection_next_by_info(cd, info);
            if (!r_info) r_info = collection_prev_by_info(cd, info);
            if (r_info) image_prebuffer_set(vw->imd, r_info->fd);
        }
    }
    else if (list)
    {
        view_window_set_list(vw, list);
        vw->list_pointer = vw->list;
        image_change_fd(vw->imd, (FileData *)vw->list->data, image_zoom_get_default(NULL));
        /* Set fd to first in list */
        fd = vw->list->data;

        if (options->image.enable_read_ahead)
        {
            GList *image_readahead = vw->list->next;
            if (image_readahead)
                image_prebuffer_set(vw->imd, (FileData *)image_readahead->data);
        }
    }
    else
    {
        image_change_fd(vw->imd, fd, image_zoom_get_default(NULL));
    }

    /* Wait until image is loaded otherwise size is not defined */
    image_load_dimensions(fd, &w, &h);

    if (options->image.limit_window_size)
    {
        gint mw = gdk_screen_width()  * options->image.max_window_size / 100;
        gint mh = gdk_screen_height() * options->image.max_window_size / 100;

        if (w > mw) w = mw;
        if (h > mh) h = mh;
    }

    gtk_window_set_default_size(GTK_WINDOW(vw->window), w, h);
    req_size.x = req_size.y = 0;
    req_size.width  = w;
    req_size.height = h;
    gtk_widget_size_allocate(GTK_WIDGET(vw->window), &req_size);

    gtk_widget_set_size_request(vw->imd->pr, w, h);

    gtk_widget_show(vw->window);

    view_window_list = g_list_append(view_window_list, vw);

    file_data_register_notify_func(view_window_notify_cb, vw, NOTIFY_PRIORITY_LOW);

    return vw;
}

static void view_window_collection_unref_cb(GtkWidget *widget, gpointer data)
{
    CollectionData *cd = data;

    collection_unref(cd);
}

void view_window_new(FileData *fd)
{
    GList *list;
    if (!fd) return;

    if (file_extension_match(fd->path, GQ_COLLECTION_EXT))
    {
        ViewWindow *vw;
        CollectionData *cd;
        CollectInfo *info = NULL;

        cd = collection_new(fd->path);
        if (collection_load(cd, fd->path, COLLECTION_LOAD_NONE))
            info = collection_get_first(cd);
        else
            g_clear_pointer(&cd, collection_unref);

        vw = real_view_window_new(NULL, NULL, cd, info);
        if (vw && cd)
            g_signal_connect(G_OBJECT(vw->window), "destroy",
                             G_CALLBACK(view_window_collection_unref_cb), cd);
    }
    else if (isdir(fd->path) && filelist_read(fd, &list, NULL))
    {
        list = filelist_sort_path(list);
        list = filelist_filter(list, FALSE);
        real_view_window_new(NULL, list, NULL, NULL);
        filelist_free(list);
    }
    else
    {
        real_view_window_new(fd, NULL, NULL, NULL);
    }
}

void view_window_new_from_list(GList *list)
{
    real_view_window_new(NULL, list, NULL, NULL);
}

void view_window_new_from_collection(CollectionData *cd, CollectInfo *info)
{
    real_view_window_new(NULL, NULL, cd, info);
}

/*
 *-----------------------------------------------------------------------------
 * public
 *-----------------------------------------------------------------------------
 */

void view_window_colors_update(void)
{
    for (GList *work = view_window_list; work; work = work->next)
    {
        ViewWindow *vw = work->data;
        image_background_set_color_from_options(vw->imd, !!vw->fs);
    }
}

gboolean view_window_find_image(ImageWindow *imd, gint *index, gint *total)
{
    for (GList *work = view_window_list; work; work = work->next)
    {
        ViewWindow *vw = work->data;

        if (vw->imd == imd ||
            (vw->fs && vw->fs->imd == imd))
        {
            if (vw->ss)
            {
                gint n;
                gint t;

                n = g_list_length(vw->ss->list_done);
                t = n + g_list_length(vw->ss->list);
                if (n == 0) n = t;
                if (index) *index = n - 1;
                if (total) *total = t;
            }
            else
            {
                if (index) *index = g_list_position(vw->list, vw->list_pointer);
                if (total) *total = g_list_length(vw->list);
            }
            return TRUE;
        }
    }

    return FALSE;
}

/*
 *-----------------------------------------------------------------------------
 * view window menu routines and callbacks
 *-----------------------------------------------------------------------------
 */

static void view_new_window_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;
    CollectionData *cd;
    CollectInfo *info;

    cd = image_get_collection(vw->imd, &info);

    if (cd && info)
        view_window_new_from_collection(cd, info);
    else
        view_window_new(image_get_fd(vw->imd));
}

static void view_edit_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw;
    ImageWindow *imd;
    const gchar *key = data;

    vw = submenu_item_get_data(widget);
    if (!vw) return;

    if (!editor_window_flag_set(key))
        view_fullscreen_toggle(vw, TRUE);

    imd = view_window_active_image(vw);
    file_util_start_editor_from_file(key, image_get_fd(imd), imd->widget);
}

static void view_alter_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw;
    AlterType type;

    vw = submenu_item_get_data(widget);
    type = GPOINTER_TO_INT(data);

    if (!vw) return;
    image_alter_orientation(vw->imd, type);
}

static void view_wallpaper_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;

    imd = view_window_active_image(vw);
    image_to_root_window(imd, (image_zoom_get(imd) == 0.0));
}

static void view_zoom_in_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;

    image_zoom_adjust(view_window_active_image(vw), get_zoom_increment());
}

static void view_zoom_out_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;

    image_zoom_adjust(view_window_active_image(vw), -get_zoom_increment());
}

static void view_zoom_1_1_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;

    image_zoom_set(view_window_active_image(vw), 1.0);
}

static void view_zoom_to_rectangle_cb(GtkWidget *widget, gpointer data)
{
    image_start_rectangle_zoom(view_window_active_image(data));
}

static void view_zoom_fit_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;

    image_zoom_set(view_window_active_image(vw), 0.0);
}

static void view_copy_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;

    imd = view_window_active_image(vw);
    file_util_copy(image_get_fd(imd), NULL, NULL, imd->widget);
}

static void view_move_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;

    imd = view_window_active_image(vw);
    file_util_move(image_get_fd(imd), NULL, NULL, imd->widget);
}

static void view_rename_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;

    imd = view_window_active_image(vw);
    file_util_rename(image_get_fd(imd), NULL, imd->widget);
}

static void view_delete_key_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;

    if (!options->file_ops.enable_delete_key)
        return;
    imd = view_window_active_image(vw);
    file_util_delete(image_get_fd(imd), NULL, imd->widget);
}

static void view_delete_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;

    imd = view_window_active_image(vw);
    file_util_delete(image_get_fd(imd), NULL, imd->widget);
}

static void view_copy_path_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;

    imd = view_window_active_image(vw);
    file_util_copy_path_to_clipboard(image_get_fd(imd));
}

static void view_fullscreen_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;

    view_fullscreen_toggle(vw, FALSE);
}

static void view_slideshow_start_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;

    view_slideshow_start(vw);
}

static void view_slideshow_stop_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;

    view_slideshow_stop(vw);
}

static void view_slideshow_pause_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;

    slideshow_pause_toggle(vw->ss);
}

static void view_close_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;

    view_window_close(vw);
}

/* accel callbacks for keys that already have a (widget, data) menu
   callback: chain to it, the widget argument is unused there */
#define VIEW_ACCEL_CHAIN_CB(name, target)                                \
    static void name(GtkAccelGroup *group, GObject *obj,                 \
                     guint keyval, GdkModifierType mod,                  \
                     gpointer data)                                      \
    {                                                                    \
        target(NULL, data);                                              \
    }

VIEW_ACCEL_CHAIN_CB(view_accel_zoom_in_cb,          view_zoom_in_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_zoom_out_cb,         view_zoom_out_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_zoom_fit_cb,         view_zoom_fit_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_zoom_1_1_cb,         view_zoom_1_1_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_zoom_to_rectangle_cb,view_zoom_to_rectangle_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_slideshow_pause_cb,  view_slideshow_pause_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_fullscreen_cb,       view_fullscreen_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_copy_cb,             view_copy_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_move_cb,             view_move_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_rename_cb,           view_rename_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_delete_key_cb,       view_delete_key_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_copy_path_cb,        view_copy_path_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_new_window_cb,       view_new_window_cb)
VIEW_ACCEL_CHAIN_CB(view_accel_close_cb,            view_close_cb)

static LayoutWindow *view_new_layout_with_fd(FileData *fd)
{
    LayoutWindow *nw;

    nw = layout_new(NULL, NULL);
    layout_sort_set(nw, options->file_sort.method, options->file_sort.ascending);
    layout_set_fd(nw, fd);
    return nw;
}


static void view_set_layout_path_cb(GtkWidget *widget, gpointer data)
{
    ViewWindow *vw = data;
    LayoutWindow *lw;
    ImageWindow *imd;

    imd = view_window_active_image(vw);

    if (!imd || !image_get_fd(imd)) return;

    lw = layout_find_by_image_fd(imd);
    if (lw)
        layout_set_fd(lw, image_get_fd(imd));
    else
        view_new_layout_with_fd(image_get_fd(imd));
    view_window_close(vw);
}

static void view_popup_menu_destroy_cb(GtkWidget *widget, gpointer data)
{
    GList *editmenu_fd_list = data;

    filelist_free(editmenu_fd_list);
}

static GList *view_window_get_fd_list(ViewWindow *vw)
{
    GList *list = NULL;
    ImageWindow *imd = view_window_active_image(vw);

    if (imd)
    {
        FileData *fd = image_get_fd(imd);
        if (fd) list = g_list_append(NULL, file_data_ref(fd));
    }

    return list;
}

/* XXX duplicated from layout_menu */
static GtkWidget *view_popup_menu(ViewWindow *vw)
{
    GtkWidget *menu;
    GtkWidget *item;
    GList *editmenu_fd_list;

    menu = popup_menu_short_lived();


    menu_item_add_stock(menu, _("Zoom _in"), GTK_STOCK_ZOOM_IN,
                        G_CALLBACK(view_zoom_in_cb), vw);
    menu_item_add_stock(menu, _("Zoom _out"), GTK_STOCK_ZOOM_OUT,
                        G_CALLBACK(view_zoom_out_cb), vw);
    menu_item_add_stock(menu, _("Zoom _1:1"), GTK_STOCK_ZOOM_100,
                        G_CALLBACK(view_zoom_1_1_cb), vw);
    menu_item_add_stock(menu, _("Zoom to _Rectangle"), GTK_STOCK_ZOOM_IN,
                        G_CALLBACK(view_zoom_to_rectangle_cb), vw);
    menu_item_add_stock(menu, _("Fit image to _window"), GTK_STOCK_ZOOM_FIT,
                        G_CALLBACK(view_zoom_fit_cb), vw);
    menu_item_add_divider(menu);

    editmenu_fd_list = view_window_get_fd_list(vw);
    g_signal_connect(G_OBJECT(menu), "destroy",
             G_CALLBACK(view_popup_menu_destroy_cb), editmenu_fd_list);
    item = submenu_add_edit(menu, NULL,
                            G_CALLBACK(view_edit_cb), vw, editmenu_fd_list);
    menu_item_add_divider(item);
    menu_item_add(item, _("Set as _wallpaper"),
                  G_CALLBACK(view_wallpaper_cb), vw);

    submenu_add_alter(menu,
                      G_CALLBACK(view_alter_cb), vw);

    menu_item_add_stock(menu, _("View in _new window"), GTK_STOCK_NEW,
                        G_CALLBACK(view_new_window_cb), vw);

    menu_item_add_divider(menu);
    menu_item_add_stock(menu, _("_Copy..."), GTK_STOCK_COPY,
                        G_CALLBACK(view_copy_cb), vw);
    menu_item_add(menu, _("_Move..."),
                  G_CALLBACK(view_move_cb), vw);
    menu_item_add(menu, _("_Rename..."),
                  G_CALLBACK(view_rename_cb), vw);
    menu_item_add_stock(menu, _("_Delete..."), GTK_STOCK_DELETE,
                        G_CALLBACK(view_delete_cb), vw);
    menu_item_add(menu, _("_Copy path"),
                  G_CALLBACK(view_copy_path_cb), vw);

    menu_item_add_divider(menu);

    if (vw->ss)
    {
        menu_item_add(menu, _("_Stop slideshow"),
                      G_CALLBACK(view_slideshow_stop_cb), vw);
        if (slideshow_paused(vw->ss))
        {
            item = menu_item_add(menu, _("Continue slides_how"),
                         G_CALLBACK(view_slideshow_pause_cb), vw);
        }
        else
        {
            item = menu_item_add(menu, _("Pause slides_how"),
                         G_CALLBACK(view_slideshow_pause_cb), vw);
        }
    }
    else
    {
        item = menu_item_add(menu, _("_Start slideshow"),
                             G_CALLBACK(view_slideshow_start_cb), vw);
        gtk_widget_set_sensitive(item, (vw->list != NULL) || view_window_contains_collection(vw));
        item = menu_item_add(menu, _("Pause slides_how"),
                             G_CALLBACK(view_slideshow_pause_cb), vw);
        gtk_widget_set_sensitive(item, FALSE);
    }

    if (vw->fs)
    {
        menu_item_add(menu, _("Exit _full screen"),
                      G_CALLBACK(view_fullscreen_cb), vw);
    }
    else
    {
        menu_item_add(menu, _("_Full screen"),
                      G_CALLBACK(view_fullscreen_cb), vw);
    }

    menu_item_add_divider(menu);
    menu_item_add_stock(menu, _("C_lose window"), GTK_STOCK_CLOSE,
                        G_CALLBACK(view_close_cb), vw);

    return menu;
}

typedef struct {
    const gchar *path;
    GCallback func;
} ViewAccel;

/* path strings must match the GtkActionEntry names in layout_util.c
   verbatim ("<Actions>/<group>/<name>"); these inherit both the default
   accelerators and any user remapping */
static const ViewAccel view_accels[] = {
    { "<Actions>/MenuActions/PrevImage",       G_CALLBACK(view_accel_step_prev_cb) },
    { "<Actions>/MenuActions/PrevImageAlt1",   G_CALLBACK(view_accel_step_prev_cb) },
    { "<Actions>/MenuActions/PrevImageAlt2",   G_CALLBACK(view_accel_step_prev_cb) },
    { "<Actions>/MenuActions/NextImage",       G_CALLBACK(view_accel_step_next_cb) },
    { "<Actions>/MenuActions/NextImageAlt1",   G_CALLBACK(view_accel_step_next_cb) },
    { "<Actions>/MenuActions/NextImageAlt2",   G_CALLBACK(view_accel_step_next_cb) },
    { "<Actions>/MenuActions/FirstImage",      G_CALLBACK(view_accel_step_first_cb) },
    { "<Actions>/MenuActions/LastImage",       G_CALLBACK(view_accel_step_last_cb) },

    { "<Actions>/MenuActions/ZoomIn",          G_CALLBACK(view_accel_zoom_in_cb) },
    { "<Actions>/MenuActions/ZoomInAlt1",      G_CALLBACK(view_accel_zoom_in_cb) },
    { "<Actions>/MenuActions/ZoomOut",         G_CALLBACK(view_accel_zoom_out_cb) },
    { "<Actions>/MenuActions/ZoomOutAlt1",     G_CALLBACK(view_accel_zoom_out_cb) },
    { "<Actions>/MenuActions/ZoomFit",         G_CALLBACK(view_accel_zoom_fit_cb) },
    { "<Actions>/MenuActions/ZoomFitAlt1",     G_CALLBACK(view_accel_zoom_fit_cb) },
    { "<Actions>/MenuActions/Zoom100",         G_CALLBACK(view_accel_zoom_1_1_cb) },
    { "<Actions>/MenuActions/Zoom100Alt1",     G_CALLBACK(view_accel_zoom_1_1_cb) },
    { "<Actions>/MenuActions/Zoom200",         G_CALLBACK(view_accel_zoom_2_1_cb) },
    { "<Actions>/MenuActions/Zoom300",         G_CALLBACK(view_accel_zoom_3_1_cb) },
    { "<Actions>/MenuActions/Zoom400",         G_CALLBACK(view_accel_zoom_4_1_cb) },
    { "<Actions>/MenuActions/Zoom50",          G_CALLBACK(view_accel_zoom_1_2_cb) },
    { "<Actions>/MenuActions/Zoom33",          G_CALLBACK(view_accel_zoom_1_3_cb) },
    { "<Actions>/MenuActions/Zoom25",          G_CALLBACK(view_accel_zoom_1_4_cb) },
    { "<Actions>/MenuActions/ZoomFillVert",    G_CALLBACK(view_accel_fill_vert_cb) },
    { "<Actions>/MenuActions/ZoomFillHor",     G_CALLBACK(view_accel_fill_horz_cb) },
    { "<Actions>/MenuActions/ZoomToRectangle", G_CALLBACK(view_accel_zoom_to_rectangle_cb) },

    { "<Actions>/MenuActions/RotateCW",        G_CALLBACK(view_accel_rotate_cw_cb) },
    { "<Actions>/MenuActions/RotateCCW",       G_CALLBACK(view_accel_rotate_ccw_cb) },
    { "<Actions>/MenuActions/Rotate180",       G_CALLBACK(view_accel_rotate_180_cb) },
    { "<Actions>/MenuActions/Mirror",          G_CALLBACK(view_accel_mirror_cb) },
    { "<Actions>/MenuActions/Flip",            G_CALLBACK(view_accel_flip_cb) },
    { "<Actions>/MenuActions/Grayscale",       G_CALLBACK(view_accel_desaturate_cb) },

    { "<Actions>/MenuActions/Refresh",         G_CALLBACK(view_accel_reload_cb) },
    { "<Actions>/MenuActions/SlideShow",       G_CALLBACK(view_accel_slideshow_toggle_cb) },
    { "<Actions>/MenuActions/SlideShowPause",  G_CALLBACK(view_accel_slideshow_pause_cb) },
    { "<Actions>/MenuActions/FullScreen",      G_CALLBACK(view_accel_fullscreen_cb) },
    { "<Actions>/MenuActions/FullScreenAlt1",  G_CALLBACK(view_accel_fullscreen_cb) },
    { "<Actions>/MenuActions/FullScreenAlt2",  G_CALLBACK(view_accel_fullscreen_cb) },
    { "<Actions>/MenuActions/ImageOverlayCycle",G_CALLBACK(view_accel_overlay_cb) },

    { "<Actions>/MenuActions/Copy",            G_CALLBACK(view_accel_copy_cb) },
    { "<Actions>/MenuActions/Move",            G_CALLBACK(view_accel_move_cb) },
    { "<Actions>/MenuActions/Rename",          G_CALLBACK(view_accel_rename_cb) },
    { "<Actions>/MenuActions/Delete",          G_CALLBACK(view_accel_delete_key_cb) },
    { "<Actions>/MenuActions/DeleteAlt1",      G_CALLBACK(view_accel_delete_key_cb) },
    { "<Actions>/MenuActions/DeleteAlt2",      G_CALLBACK(view_accel_delete_key_cb) },
    { "<Actions>/MenuActions/CopyPath",        G_CALLBACK(view_accel_copy_path_cb) },

    { "<Actions>/MenuActions/Print",           G_CALLBACK(view_accel_print_cb) },
    { "<Actions>/MenuActions/ViewInNewWindow", G_CALLBACK(view_accel_new_window_cb) },
    { "<Actions>/MenuActions/Preferences",     G_CALLBACK(view_accel_config_cb) },

    { "<Actions>/MenuActions/Escape",          G_CALLBACK(view_accel_escape_cb) },
    { "<Actions>/MenuActions/EscapeAlt1",      G_CALLBACK(view_accel_escape_cb) },
    { "<Actions>/MenuActions/CloseWindow",     G_CALLBACK(view_accel_close_cb) },
};

static void view_window_accels_init(ViewWindow *vw)
{
    GtkAccelGroup *group;
    guint i;

    group = gtk_accel_group_new();

    for (i = 0; i < G_N_ELEMENTS(view_accels); i++)
    {
        if (!gtk_accel_map_lookup_entry(view_accels[i].path, NULL))
        {
            /* no entry means the layout action was never built —
               warn rather than silently binding nothing */
            g_warning("no accel entry for %s", view_accels[i].path);
            continue;
        }
        gtk_accel_group_connect_by_path(group,
                                        view_accels[i].path,
                                        g_cclosure_new(view_accels[i].func, vw, NULL));
    }

    gtk_window_add_accel_group(GTK_WINDOW(vw->window), group);
}

typedef void (*ViewAccelFunc)(GtkAccelGroup *group, GObject *acceleratable,  
                              guint keyval, GdkModifierType mod, gpointer data);  

static gboolean mouse_binding_activate(ViewWindow *vw,
                                       guint button, GdkModifierType state)
{
    state &= gtk_accelerator_get_default_mod_mask();
    for (GList *work = options->mouse_bindings; work; work = work->next)
    {
        MouseBinding *mb = work->data;

        if (mb->button != button ||
            mb->state  != state) continue;

        for (gint i = 0; i < G_N_ELEMENTS(view_accels); i++)  
            if (strcmp(mb->action_name, view_accels[i].path +
                                        strlen("<Actions>/MenuActions/")) == 0)  
            {  
                ((ViewAccelFunc)view_accels[i].func)(NULL, NULL, 0, 0, vw);  
                return TRUE;  
            }  
    }

    return FALSE;
}


/*
 *-------------------------------------------------------------------
 * dnd confirm dir
 *-------------------------------------------------------------------
 */

typedef struct {
    ViewWindow *vw;
    GList *list;
} CViewConfirmD;

static void view_dir_list_cancel(GtkWidget *widget, gpointer data)
{
    /* do nothing */
}

static void view_dir_list_do(ViewWindow *vw, GList *list, gboolean skip, gboolean recurse)
{
    view_window_set_list(vw, NULL);

    for (GList *work = list; work; work = work->next)
    {
        FileData *fd = work->data;

        if (isdir(fd->path))
        {
            if (!skip)
            {
                GList *list = NULL;

                if (recurse)
                {
                    list = filelist_recursive(fd);
                }
                else
                { /*FIXME */
                    filelist_read(fd, &list, NULL);
                    list = filelist_sort_path(list);
                    list = filelist_filter(list, FALSE);
                }
                if (list)
                    vw->list = g_list_concat(vw->list, list);
            }
        }
        else
        {
            /* FIXME: no filtering here */
            vw->list = g_list_append(vw->list, file_data_ref(fd));
        }
    }

    if (vw->list)
    {
        FileData *fd = vw->list->data;

        vw->list_pointer = vw->list;
        image_change_fd(vw->imd, fd, image_zoom_get_default(vw->imd));

        GList *image_readahead = vw->list->next;
        if (options->image.enable_read_ahead && image_readahead)
        {
            fd = image_readahead->data;
            image_prebuffer_set(vw->imd, fd);
        }
    }
    else
    {
        image_change_fd(vw->imd, NULL, image_zoom_get_default(vw->imd));
    }
}

static void view_dir_list_add(GtkWidget *widget, gpointer data)
{
    CViewConfirmD *d = data;
    view_dir_list_do(d->vw, d->list, FALSE, FALSE);
}

static void view_dir_list_recurse(GtkWidget *widget, gpointer data)
{
    CViewConfirmD *d = data;
    view_dir_list_do(d->vw, d->list, FALSE, TRUE);
}

static void view_dir_list_skip(GtkWidget *widget, gpointer data)
{
    CViewConfirmD *d = data;
    view_dir_list_do(d->vw, d->list, TRUE, FALSE);
}

static void view_dir_list_destroy(GtkWidget *widget, gpointer data)
{
    CViewConfirmD *d = data;
    filelist_free(d->list);
    g_free(d);
}

static GtkWidget *view_confirm_dir_list(ViewWindow *vw, GList *list)
{
    GtkWidget *menu;
    CViewConfirmD *d;

    d = g_new(CViewConfirmD, 1);
    d->vw = vw;
    d->list = list;

    menu = popup_menu_short_lived();
    g_signal_connect(G_OBJECT(menu), "destroy",
             G_CALLBACK(view_dir_list_destroy), d);

    menu_item_add_stock(menu, _("Dropped list includes folders."),
                        GTK_STOCK_DND_MULTIPLE, NULL, NULL);
    menu_item_add_divider(menu);
    menu_item_add_stock(menu, _("_Add contents"), GTK_STOCK_OK,
                        G_CALLBACK(view_dir_list_add), d);
    menu_item_add_stock(menu, _("Add contents _recursive"), GTK_STOCK_ADD,
                        G_CALLBACK(view_dir_list_recurse), d);
    menu_item_add_stock(menu, _("_Skip folders"), GTK_STOCK_REMOVE,
                        G_CALLBACK(view_dir_list_skip), d);
    menu_item_add_divider(menu);
    menu_item_add_stock(menu, _("Cancel"), GTK_STOCK_CANCEL,
                        G_CALLBACK(view_dir_list_cancel), d);

    return menu;
}

/*
 *-----------------------------------------------------------------------------
 * image drag and drop routines
 *-----------------------------------------------------------------------------
 */

static void view_window_get_dnd_data(GtkWidget *widget, GdkDragContext *context,
                                     gint x, gint y,
                                     GtkSelectionData *selection_data, guint info,
                                     guint time, gpointer data)
{
    ViewWindow *vw = data;
    ImageWindow *imd;

    if (gtk_drag_get_source_widget(context) == vw->imd->pr) return;

    imd = vw->imd;

    if (info == TARGET_URI_LIST ||
        info == TARGET_APP_COLLECTION_MEMBER)
    {
        CollectionData *source = NULL;
        GList *list = NULL;
        GList *info_list = NULL;

        if (info == TARGET_URI_LIST)
        {
            list = uri_filelist_from_gtk_selection_data(selection_data);

            for (GList *work = list; work; work = work->next)
            {
                FileData *fd = work->data;
                if (isdir(fd->path))
                {
                    GtkWidget *menu = view_confirm_dir_list(vw, list);
                    GdkEventButton event;
                    widget_coords_to_root(widget, x, y, &event.x_root, &event.y_root);
                    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, popup_menu_at_event,
                                   &event, 0, time);
                    return;
                }
            }

            list = filelist_filter(list, FALSE);
        }
        else
        {
            source = collection_from_dnd_data(
                     (gchar *)gtk_selection_data_get_data(selection_data),
                     &list, &info_list);
        }

        if (list)
        {
            FileData *fd = list->data;
            if (isfile(fd->path))
            {
                view_slideshow_stop(vw);
                view_window_set_list(vw, NULL);

                if (source && info_list)
                {
                    image_change_from_collection(imd, source, info_list->data,
                                                 image_zoom_get_default(imd));
                }
                else
                {
                    if (list->next)
                        vw->list_pointer = vw->list = g_steal_pointer(&list);

                    image_change_fd(imd, fd, image_zoom_get_default(imd));
                }
            }
        }
        filelist_free(list);
        g_list_free(info_list);
    }
}

static void view_window_set_dnd_data(GtkWidget *widget, GdkDragContext *context,
                                     GtkSelectionData *selection_data, guint info,
                                     guint time, gpointer data)
{
    ViewWindow *vw = data;
    FileData *fd;

    fd = image_get_fd(vw->imd);

    if (fd)
    {
        GList *list = g_list_append(NULL, fd);
        uri_selection_data_set_uris_from_filelist(selection_data, list);
        g_list_free(list);
    }
    else
    {
        gtk_selection_data_set(selection_data,
                               gtk_selection_data_get_target(selection_data),
                               8, NULL, 0);
    }
}

static void view_window_dnd_init(ViewWindow *vw)
{
    ImageWindow *imd = vw->imd;

    gtk_drag_source_set(imd->pr, GDK_BUTTON2_MASK,
                        dnd_file_drag_types, dnd_file_drag_types_count,
                        GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
    g_signal_connect(G_OBJECT(imd->pr), "drag_data_get",
                     G_CALLBACK(view_window_set_dnd_data), vw);

    gtk_drag_dest_set(imd->pr,
                      GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP,
                      dnd_file_drop_types, dnd_file_drop_types_count,
                      GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
    g_signal_connect(G_OBJECT(imd->pr), "drag_data_received",
                     G_CALLBACK(view_window_get_dnd_data), vw);
}

/*
 *-----------------------------------------------------------------------------
 * maintenance (for rename, move, remove)
 *-----------------------------------------------------------------------------
 */

static void view_real_removed(ViewWindow *vw, FileData *fd)
{
    ImageWindow *imd = view_window_active_image(vw);
    FileData *image_fd = image_get_fd(imd);

    if (image_fd && image_fd == fd)
    {
        if (vw->list)
        {
            view_list_step(vw, TRUE);
            if (image_get_fd(imd) == image_fd)
                view_list_step(vw, FALSE);
        }
        else if (view_window_contains_collection(vw))
        {
            view_collection_step(vw, TRUE);
            if (image_get_fd(imd) == image_fd)
                view_collection_step(vw, FALSE);
        }
        if (image_get_fd(imd) == image_fd)
            image_change_fd(imd, NULL, image_zoom_get_default(imd));
    }
    /* XXX handle slideshow too */

    if (vw->list)
    {
        for (GList *work = vw->list, *next; work; work = next)
        {
            FileData *chk_fd = work->data;
            next = work->next;

            if (chk_fd == fd)
            {
                if (vw->list_pointer == work)
                    vw->list_pointer = next ? next : work->prev;
                vw->list = g_list_delete_link(vw->list, work);
                file_data_unref(chk_fd);
            }
        }

        image_change_fd(imd, vw->list_pointer ? vw->list_pointer->data : NULL,
                        image_zoom_get_default(imd));
    }

    image_osd_update(imd);
}

static void view_window_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
    ViewWindow *vw = data;

    if (!(type & NOTIFY_CHANGE) || !fd->change) return;

    DEBUG_1("Notify view_window: %s %04x", fd->path, type);

    switch (fd->change->type)
    {
        case FILEDATA_CHANGE_MOVE:
        case FILEDATA_CHANGE_RENAME:
            break;
        case FILEDATA_CHANGE_COPY:
            break;
        case FILEDATA_CHANGE_DELETE:
            view_real_removed(vw, fd);
            break;
        case FILEDATA_CHANGE_UNSPECIFIED:
        case FILEDATA_CHANGE_WRITE_METADATA:
            break;
    }
}
