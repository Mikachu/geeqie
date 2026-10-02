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

#ifndef LAYOUT_H
#define LAYOUT_H

#include "main.h"
#include "filedata.h"
#include "view_dir.h"
#include "view_file.h"

#define LAYOUT_ID_CURRENT "_current_"
#define MAX_SPLIT_IMAGES 4

typedef enum {
    SPLIT_NONE = 0,
    SPLIT_VERT,
    SPLIT_HORZ,
    SPLIT_QUAD,
} ImageSplitMode;

typedef enum {
    STARTUP_PATH_CURRENT = 0,
    STARTUP_PATH_LAST,
    STARTUP_PATH_HOME,
} StartUpPath;

typedef enum {
    TOOLBAR_MAIN,
    TOOLBAR_STATUS,
    TOOLBAR_COUNT
} ToolbarType;

typedef enum {
    LAYOUT_HIDE   = 0,
    LAYOUT_LEFT   = 1 << 0,
    LAYOUT_RIGHT  = 1 << 1,
    LAYOUT_TOP    = 1 << 2,
    LAYOUT_BOTTOM = 1 << 3
} LayoutLocation;

typedef struct LayoutOptions LayoutOptions;
struct LayoutOptions
{
    gchar *id;

    gchar *order;
    gint style;

    DirViewType dir_view_type;
    FileViewType file_view_type;

    struct {
        SortType method;
        gboolean ascend;
    } dir_view_list_sort;

    gboolean show_thumbnails;
    gboolean show_marks;
    gboolean show_directory_date;
    gboolean show_info_pixel;

    struct {
        gint w;
        gint h;
        gint x;
        gint y;
        gboolean maximized;
        gint hdivider_pos;
        gint vdivider_pos;
    } main_window;

    struct {
        gint w;
        gint h;
        gint x;
        gint y;
        gint vdivider_pos;
    } float_window;

    struct {
        gint w;
        gint h;
    } properties_window;

    struct {
        guint state;
        gint histogram_channel;
        gint histogram_mode;
    } image_overlay;

    gboolean tools_float;
    gboolean tools_hidden;
    gboolean toolbar_hidden;

    gchar *home_path;

    StartUpPath startup_path;

    gboolean exit_on_close;
};

struct SlideShowData;
struct ImageWindow;
struct FullScreenData;

typedef struct LayoutWindow LayoutWindow;
struct LayoutWindow
{
    LayoutOptions options;

    FileData *dir_fd;
    FileData *image_pending_fd;
    guint image_pending_idle_id;

    /* base */

    GtkWidget *window;

    GtkWidget *main_box;

    GtkWidget *group_box;
    GtkWidget *h_pane;
    GtkWidget *v_pane;

    /* menus, path selector */

    GtkActionGroup *action_group;
    GtkActionGroup *action_group_editors;
    guint ui_editors_id;
    GtkUIManager *ui_manager;
    GList *toolbar_actions[TOOLBAR_COUNT];

    GtkWidget *path_entry;

    /* image */

    LayoutLocation image_location;

    struct ImageWindow *image;

    struct ImageWindow *split_images[MAX_SPLIT_IMAGES];
    ImageSplitMode split_mode;
    gint active_split_image;

    GtkWidget *split_image_widget;
    GtkSizeGroup *split_image_sizegroup;

    /* tools window (float) */

    GtkWidget *tools;
    GtkWidget *tools_pane;

//  gint tools_float;
//  gint tools_hidden;

    GtkWidget *menu_bar; /* referenced by lw, exist during whole lw lifetime */
    /* toolbar */

    GtkWidget *toolbar[TOOLBAR_COUNT]; /* referenced by lw, exist during whole lw lifetime */
//  gint toolbar_hidden;

//  GtkWidget *thumb_button;
//  gint thumbs_enabled;
//  gint marks_enabled;

    GtkWidget *back_button;

    /* dir view */

    LayoutLocation dir_location;

    ViewDir *vd;
    GtkWidget *dir_view;

//  DirViewType dir_view_type;

    /* file view */

    LayoutLocation file_location;

    ViewFile *vf;
//  FileViewType file_view_type;

    GtkWidget *file_view;

    SortType sort_method;
    gboolean sort_ascend;

    /* status bar */

    GtkWidget *info_box;
    GtkWidget *info_progress_bar;
    GtkWidget *info_sort;
    GtkWidget *info_status;
    GtkWidget *info_details;
    GtkWidget *info_zoom;
    GtkWidget *info_pixel;

    /* slide show */

    struct SlideShowData *slideshow;

    /* full screen */

    struct FullScreenData *full_screen;

    /* notebook used to switch between normal layout (page 0) and fullscreen image (page 1) */
    GtkWidget *fs_notebook;
    GtkWidget *fs_page;

    /* dividers */

//  gint div_h;
//  gint div_v;
//  gint div_float;

    /* misc */

    GtkWidget *utility_box;   /* referenced by lw, exist during whole lw lifetime */
    GtkWidget *utility_paned; /* between image and bar */
    GtkWidget *bar_sort;
    GtkWidget *bar;

//  gint bar_sort_enabled;
//  gint bar_enabled;

//  gint bar_width;

    GtkWidget *exif_window;
};

extern GList *layout_window_list;

LayoutWindow *layout_new(FileData *dir_fd, LayoutOptions *lop);
LayoutWindow *layout_new_with_geometry(FileData *dir_fd, LayoutOptions *lop,
                                       const gchar *geometry);
LayoutWindow *layout_new_from_config(const gchar **attribute_names,
                                     const gchar **attribute_values, gboolean use_commandline);
void layout_update_from_config(LayoutWindow *lw, const gchar **attribute_names,
                                                 const gchar **attribute_values);

void layout_close(LayoutWindow *lw);
void layout_free(LayoutWindow *lw);

gboolean layout_valid(LayoutWindow **lw);

void layout_show_config_window(LayoutWindow *lw);

void layout_apply_options(LayoutWindow *lw, LayoutOptions *lop);

void layout_sync_options_with_current_state(LayoutWindow *lw);
void layout_load_attributes(LayoutOptions *layout, const gchar **attribute_names,
                                                   const gchar **attribute_values);
void layout_write_attributes(LayoutOptions *layout, GString *outstr, gint indent);
void layout_write_config(LayoutWindow *lw, GString *outstr, gint indent);

LayoutWindow *layout_find_by_image(struct ImageWindow *imd);
LayoutWindow *layout_find_by_image_fd(struct ImageWindow *imd);
LayoutWindow *layout_find_by_layout_id(const gchar *id);

const gchar *layout_get_path(LayoutWindow *lw);
gboolean layout_set_path(LayoutWindow *lw, const gchar *path);
gboolean layout_set_fd(LayoutWindow *lw, FileData *fd);

void layout_status_update_progress(LayoutWindow *lw, gdouble val, const gchar *text);
void layout_status_update_info(LayoutWindow *lw, const gchar *text);
void layout_status_update_image(LayoutWindow *lw);
void layout_status_update_all(LayoutWindow *lw);

GList *layout_list(LayoutWindow *lw);
guint layout_list_count(LayoutWindow *lw, gint64 *bytes);
FileData *layout_list_get_fd(LayoutWindow *lw, gint index);
gint layout_list_get_index(LayoutWindow *lw, FileData *fd);
void layout_list_sync_fd(LayoutWindow *lw, FileData *fd);

GList *layout_selection_list(LayoutWindow *lw);
/* return list of pointers to int for selection */
GList *layout_selection_list_by_index(LayoutWindow *lw);
guint layout_selection_count(LayoutWindow *lw, gint64 *bytes);
void layout_select_all(LayoutWindow *lw);
void layout_select_none(LayoutWindow *lw);
void layout_select_invert(LayoutWindow *lw);

void layout_mark_to_selection(LayoutWindow *lw, gint mark, MarkToSelectionMode mode);
void layout_selection_to_mark(LayoutWindow *lw, gint mark, SelectionToMarkMode mode);

void layout_mark_filter_toggle(LayoutWindow *lw, gint mark);

void layout_refresh(LayoutWindow *lw);

void layout_thumb_set(LayoutWindow *lw, gboolean enable);
gboolean layout_thumb_get(LayoutWindow *lw);

void layout_marks_set(LayoutWindow *lw, gboolean enable);
gboolean layout_marks_get(LayoutWindow *lw);

void layout_sort_set(LayoutWindow *lw, SortType type, gboolean ascend);
gboolean layout_sort_get(LayoutWindow *lw, SortType *type, gboolean *ascend);

gboolean layout_geometry_get(LayoutWindow *lw, gint *x, gint *y, gint *w, gint *h);
gboolean layout_geometry_get_dividers(LayoutWindow *lw, gint *h, gint *v);

void layout_views_set(LayoutWindow *lw, DirViewType dir_view_type, FileViewType file_view_type);
gboolean layout_views_get(LayoutWindow *lw, DirViewType *dir_view_type, FileViewType *file_view_type);

void layout_views_set_sort(LayoutWindow *lw, SortType method, gboolean ascend);

void layout_status_update(LayoutWindow *lw, const gchar *text);

void layout_style_set(LayoutWindow *lw, gint style, const gchar *order);

void layout_menu_update_edit(void);
void layout_styles_update(void);
void layout_colors_update(void);

gboolean layout_geometry_get_tools(LayoutWindow *lw, gint *x, gint *y, gint *w, gint *h, gint *divider_pos);
void layout_tools_float_set(LayoutWindow *lw, gboolean popped, gboolean hidden);
gboolean layout_tools_float_get(LayoutWindow *lw, gboolean *popped, gboolean *hidden);

void layout_tools_float_toggle(LayoutWindow *lw);
void layout_tools_hide_toggle(LayoutWindow *lw);

void layout_toolbar_toggle(LayoutWindow *lw);
void layout_info_pixel_set(LayoutWindow *lw, gboolean show);

void layout_split_change(LayoutWindow *lw, ImageSplitMode mode);

#endif
