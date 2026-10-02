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

#ifndef FILEDATA_H
#define FILEDATA_H

#include "typedefs.h"

typedef enum {
    FILEDATA_CHANGE_DELETE,
    FILEDATA_CHANGE_MOVE,
    FILEDATA_CHANGE_RENAME,
    FILEDATA_CHANGE_COPY,
    FILEDATA_CHANGE_UNSPECIFIED,
    FILEDATA_CHANGE_WRITE_METADATA
} FileDataChangeType;

typedef enum {
    MTS_MODE_MINUS,
    MTS_MODE_SET,
    MTS_MODE_OR,
    MTS_MODE_AND
} MarkToSelectionMode;

typedef enum {
    NOTIFY_PRIORITY_HIGH = 0,
    NOTIFY_PRIORITY_MEDIUM,
    NOTIFY_PRIORITY_LOW
} NotifyPriority;

typedef enum {
    NOTIFY_MARKS        = 1 << 1, /* changed marks */
    NOTIFY_PIXBUF       = 1 << 2, /* image was read into fd->pixbuf */
    NOTIFY_HISTMAP      = 1 << 3, /* histmap was read into fd->histmap */
    NOTIFY_ORIENTATION  = 1 << 4, /* image was rotated */
    NOTIFY_METADATA     = 1 << 5, /* changed image metadata, not yet written */
    NOTIFY_GROUPING     = 1 << 6, /* change in fd->sidecar_files or fd->parent */
    NOTIFY_REREAD       = 1 << 7, /* changed file size, date, etc., file name remains unchanged */
    NOTIFY_CHANGE       = 1 << 8  /* generic change described by fd->change */
} NotifyType;

typedef enum {
    CHANGE_OK                      = 0,
    CHANGE_WARN_DEST_EXISTS        = 1 << 0,
    CHANGE_WARN_NO_WRITE_PERM      = 1 << 1,
    CHANGE_WARN_SAME               = 1 << 2,
    CHANGE_WARN_CHANGED_EXT        = 1 << 3,
    CHANGE_WARN_UNSAVED_META       = 1 << 4,
    CHANGE_WARN_NO_WRITE_PERM_DEST_DIR  = 1 << 5,
    CHANGE_ERROR_MASK              = (~0U) << 8, /* the values below are fatal errors */
    CHANGE_NO_READ_PERM            = 1 << 8,
    CHANGE_NO_WRITE_PERM_DIR       = 1 << 9,
    CHANGE_NO_DEST_DIR             = 1 << 10,
    CHANGE_DUPLICATE_DEST          = 1 << 11,
    CHANGE_NO_WRITE_PERM_DEST      = 1 << 12,
    CHANGE_DEST_EXISTS             = 1 << 13,
    CHANGE_NO_SRC                  = 1 << 14,
    CHANGE_GENERIC_ERROR           = 1 << 16
} ChangeError;
#define CHANGE_NUM_ERRORS 17

typedef enum {
    STM_MODE_RESET,
    STM_MODE_SET,
    STM_MODE_TOGGLE
} SelectionToMarkMode;

typedef enum {
    FORMAT_CLASS_UNKNOWN,
    FORMAT_CLASS_IMAGE,
    FORMAT_CLASS_RAWIMAGE,
    FORMAT_CLASS_META,
    FORMAT_CLASS_VIDEO,
    FILE_FORMAT_CLASSES
} FileFormatClass;

struct FileData {
    guint magick;
    gint type;
    gchar *original_path; /* key to file_data_pool hash table */
    gchar *path;
    const gchar *name;
    const gchar *extension;
    gchar *collate_key_name;
    gchar *collate_key_name_nocase;
    gint64 size;
    struct timespec dat;
    struct timespec cdat;
    mode_t mode;       /* this is needed at least for notification in view_dir
                          because it is preserved after the file/directory is deleted */
    gint sidecar_priority;

    guint marks;       /* each bit represents one mark */
    guint valid_marks; /* zero bit means that the corresponding mark needs to be reread */

    GList *sidecar_files;
    FileData *parent;  /* parent file if this is a sidecar file, NULL otherwise */
    FileDataChangeInfo *change; /* for rename, move ... */
    GdkPixbuf *thumb_pixbuf;

    GdkPixbuf *pixbuf; /* full-size image, only complete images, NULL during loading
                          all FileData with non-NULL pixbuf are referenced by image_cache */
    guint page_num;    /* requested sub-image/page index for multi-image files
                        * (TIFF pages, MKV image attachments, ...); 0 by default */
    guint page_total;  /* number of sub-images/pages available, as discovered by the
                        * loader backend; 0 or 1 means "not a multi-image file" */

    HistMap *histmap;

    gint ref;
    gint version;      /* increased when any field in this structure is changed */
    gboolean disable_grouping;

    gint user_orientation;
    gint exif_orientation;

    ExifData *exif;
    time_t exifdate;
    GHashTable *modified_xmp; /* hash table which contains unwritten xmp metadata
                                 in format: key->list of string values */
    GHashTable *cached_metadata;
};

struct FileDataChangeInfo {
    FileDataChangeType type;
    gchar *source;
    gchar *dest;
    gint error;
    gboolean regroup_when_finished;
};

typedef struct {
    gchar *dir_path;
    gboolean follow_symlinks;
    gboolean want_files;
    gboolean want_dirs;
    gint cancel;            /* atomic: set to 1 to cancel */
    GArray *entries;        /* output: array of DirEntry, NULL until thread fills it */
    gboolean success;
    GList *files;           /* filled by filelist_read_done_cb */
    GList *dirs;            /* filled by filelist_read_done_cb */
    guint generation;       /* vf->dir_load_generation at launch time */
    GList *old_list;        /* old vf->list to free after update */
    GSourceFunc done_cb;
    gpointer done_data;
} DirLoadData;

#ifdef DEBUG
#define DEBUG_FILEDATA
#endif

#define FD_MAGICK 0x12345678u
#define FILEDATA_MARKS_SIZE 6

gchar *text_from_size(gint64 size);
gchar *text_from_size_abrev(gint64 size);
gchar *text_from_time(time_t t);

/* these all do the same thing because this codebase is silly */
FileData *file_data_new_group(const gchar *path_utf8);
FileData *file_data_new_no_grouping(const gchar *path_utf8);
FileData *file_data_new_dir(const gchar *path_utf8);
FileData *file_data_new_simple(const gchar *path_utf8);

/* returns NULL if path_utf8 is not an existing directory */
FileData *file_data_new_dir_exist(const gchar *path_utf8);

#ifdef DEBUG_FILEDATA
FileData *file_data_ref_debug(const gchar *file, gint line, FileData *fd);
void file_data_unref_debug(const gchar *file, gint line, FileData *fd);
static inline void file_data_unref(FileData *fd) {
#define file_data_ref(fd) file_data_ref_debug(__FILE__, __LINE__, fd)
#define file_data_unref(fd) file_data_unref_debug(__FILE__, __LINE__, fd)
    file_data_unref(fd);
}
#else
FileData *file_data_ref(FileData *fd);
void file_data_unref(FileData *fd);
#endif

gboolean file_data_check_changed_files(FileData *fd);

void file_data_increment_version(FileData *fd);

gboolean file_data_add_change_info(FileData *fd, FileDataChangeType type,
                                   const gchar *src, const gchar *dest);
void file_data_change_info_free(FileDataChangeInfo *fdci, FileData *fd);

void file_data_disable_grouping(FileData *fd, gboolean disable);
void file_data_disable_grouping_list(GList *fd_list, gboolean disable);

gint filelist_sort_compare_filedata_cb(const FileData *fa, const FileData *fb, gpointer data);
gint filelist_sort_compare_filedata(FileData *fa, FileData *fb, SortType method, gboolean ascend);
GList *filelist_sort(GList *list, SortType method, gboolean ascend);
GList *filelist_insert_sort(GList *list, FileData *fd, SortType method, gboolean ascend);
GList *filelist_sort_full(GList *list, SortType method, gboolean ascend, GCompareDataFunc cb);
GList *filelist_insert_sort_full(GList *list, gpointer data, SortType method, gboolean ascend, GCompareDataFunc cb);

GThread *filelist_read_async(gpointer data);
gboolean filelist_read(FileData *dir_fd, GList **files, GList **dirs);
gboolean filelist_read_lstat(FileData *dir_fd, GList **files, GList **dirs);
void filelist_free(GList *list);
GList *filelist_copy(GList *list);
GList *filelist_from_path_list(GList *list);
GList *filelist_to_path_list(GList *list);

GList *filelist_filter(GList *list, gboolean is_dir_list);

GList *filelist_sort_path(GList *list);
GList *filelist_recursive(FileData *dir_fd);
GList *filelist_recursive_full(FileData *dir_fd, SortType method, gboolean ascend);

typedef gboolean (* FileDataGetMarkFunc)(FileData *fd, gint n, gpointer data);
typedef gboolean (* FileDataSetMarkFunc)(FileData *fd, gint n, gboolean value, gpointer data);
gboolean file_data_register_mark_func(gint n, FileDataGetMarkFunc get_mark_func,
                                      FileDataSetMarkFunc set_mark_func, gpointer data, GDestroyNotify notify);
void file_data_get_registered_mark_func(gint n, FileDataGetMarkFunc *get_mark_func,
                                        FileDataSetMarkFunc *set_mark_func, gpointer *data);

gboolean file_data_get_mark(FileData *fd, gint n);
guint file_data_get_marks(FileData *fd);
void file_data_set_mark(FileData *fd, gint n, gboolean value);
gboolean file_data_filter_marks(FileData *fd, guint filter);
GList *file_data_filter_marks_list(GList *list, guint filter);

gint file_data_get_user_orientation(FileData *fd);
void file_data_set_user_orientation(FileData *fd, gint value);

gchar *file_data_sc_list_to_string(FileData *fd);

gchar *file_data_get_sidecar_path(FileData *fd, gboolean existing_only);

gboolean file_data_add_ci(FileData *fd, FileDataChangeType type, const gchar *src, const gchar *dest);
gboolean file_data_sc_add_ci_copy(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_add_ci_move(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_add_ci_rename(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_add_ci_delete(FileData *fd);
gboolean file_data_sc_add_ci_unspecified(FileData *fd, const gchar *dest_path);

gboolean file_data_sc_add_ci_delete_list(GList *fd_list);
gboolean file_data_sc_add_ci_copy_list(GList *fd_list, const gchar *dest);
gboolean file_data_sc_add_ci_move_list(GList *fd_list, const gchar *dest);
gboolean file_data_sc_add_ci_rename_list(GList *fd_list, const gchar *dest);
gboolean file_data_sc_add_ci_unspecified_list(GList *fd_list, const gchar *dest);
gboolean file_data_add_ci_write_metadata_list(GList *fd_list);

gboolean file_data_sc_update_ci_copy_list(GList *fd_list, const gchar *dest);
gboolean file_data_sc_update_ci_move_list(GList *fd_list, const gchar *dest);
gboolean file_data_sc_update_ci_unspecified_list(GList *fd_list, const gchar *dest);


gboolean file_data_sc_update_ci_copy(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_update_ci_move(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_update_ci_rename(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_update_ci_unspecified(FileData *fd, const gchar *dest_path);

gchar *file_data_get_error_string(gint error);

gint file_data_verify_ci(FileData *fd, GList *list);
gint file_data_verify_ci_list(GList *list, gchar **desc, gboolean with_sidecars);

gboolean file_data_perform_ci(FileData *fd);
gboolean file_data_apply_ci(FileData *fd);
void file_data_free_ci(FileData *fd);
void file_data_free_ci_list(GList *fd_list);

void file_data_set_regroup_when_finished(FileData *fd, gboolean enable);

gint file_data_sc_verify_ci(FileData *fd, GList *list);

gboolean file_data_sc_perform_ci(FileData *fd);
gboolean file_data_sc_apply_ci(FileData *fd);
void file_data_sc_free_ci(FileData *fd);
void file_data_sc_free_ci_list(GList *fd_list);

GList *file_data_process_groups_in_selection(GList *list, gboolean ungroup, GList **ungrouped);

typedef void (*FileDataNotifyFunc)(FileData *fd, NotifyType type, gpointer data);
gboolean file_data_register_notify_func(FileDataNotifyFunc func, gpointer data, NotifyPriority priority);
gboolean file_data_unregister_notify_func(FileDataNotifyFunc func, gpointer data);
void file_data_send_notification(FileData *fd, NotifyType type);

#endif
