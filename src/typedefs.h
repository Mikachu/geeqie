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
    ZOOM_RESET_ORIGINAL   = 0,
    ZOOM_RESET_FIT_WINDOW = 1,
    ZOOM_RESET_NONE       = 2
} ZoomMode;

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

typedef enum {
    DIRVIEW_LIST,
    DIRVIEW_TREE
} DirViewType;

typedef enum {
    FILEVIEW_LIST,
    FILEVIEW_ICON
} FileViewType;

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

typedef enum {
    ALTER_NONE,
    ALTER_ROTATE_90,
    ALTER_ROTATE_90_CC,
    ALTER_ROTATE_180,
    ALTER_MIRROR,
    ALTER_FLIP,
} AlterType;

typedef enum {
    LAYOUT_HIDE   = 0,
    LAYOUT_LEFT   = 1 << 0,
    LAYOUT_RIGHT  = 1 << 1,
    LAYOUT_TOP    = 1 << 2,
    LAYOUT_BOTTOM = 1 << 3
} LayoutLocation;


typedef enum {
    IMAGE_STATE_NONE        = 0,
    IMAGE_STATE_IMAGE       = 1 << 0,
    IMAGE_STATE_LOADING     = 1 << 1,
    IMAGE_STATE_ERROR       = 1 << 2,
    IMAGE_STATE_COLOR_ADJ   = 1 << 3,
    IMAGE_STATE_ROTATE_AUTO = 1 << 4,
    IMAGE_STATE_ROTATE_USER = 1 << 5,
    IMAGE_STATE_DELAY_FLIP  = 1 << 6
} ImageState;

typedef enum {
    SPLIT_NONE = 0,
    SPLIT_VERT,
    SPLIT_HORZ,
    SPLIT_QUAD,
} ImageSplitMode;

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

typedef enum {
    SS_ERR_NONE = 0,
    SS_ERR_DISABLED, /**< secsave is disabled. */
    SS_ERR_OUT_OF_MEM, /**< memory allocation failure */

    /* see err field in SecureSaveInfo */
    SS_ERR_OPEN_READ,
    SS_ERR_OPEN_WRITE,
    SS_ERR_STAT,
    SS_ERR_ACCESS,
    SS_ERR_MKSTEMP,
    SS_ERR_RENAME,
    SS_ERR_OTHER,
} SecureSaveErrno;

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
    METADATA_PLAIN      = 0, /* format that can be edited and written back */
    METADATA_FORMATTED  = 1  /* for display only */
} MetadataFormat;

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
    PR_STEREO_NONE             = 0,   /* do nothing */
    PR_STEREO_DUAL             = 1 << 0, /* independent stereo buffers, for example nvidia opengl */
    PR_STEREO_FIXED            = 1 << 1,  /* custom position */
    PR_STEREO_HORIZ            = 1 << 2,  /* side by side */
    PR_STEREO_VERT             = 1 << 3,  /* above below */
    PR_STEREO_RIGHT            = 1 << 4,  /* render right buffer */
    PR_STEREO_ANAGLYPH_RC      = 1 << 5,  /* anaglyph red-cyan */
    PR_STEREO_ANAGLYPH_GM      = 1 << 6,  /* anaglyph green-magenta */
    PR_STEREO_ANAGLYPH_YB      = 1 << 7,  /* anaglyph yellow-blue */
    PR_STEREO_ANAGLYPH_GRAY_RC = 1 << 8,  /* anaglyph gray red-cyan*/
    PR_STEREO_ANAGLYPH_GRAY_GM = 1 << 9,  /* anaglyph gray green-magenta */
    PR_STEREO_ANAGLYPH_GRAY_YB = 1 << 10, /* anaglyph gray yellow-blue */
    PR_STEREO_ANAGLYPH_DB_RC   = 1 << 11, /* anaglyph dubois red-cyan */
    PR_STEREO_ANAGLYPH_DB_GM   = 1 << 12, /* anaglyph dubois green-magenta */
    PR_STEREO_ANAGLYPH_DB_YB   = 1 << 13, /* anaglyph dubois yellow-blue */
    PR_STEREO_ANAGLYPH         = PR_STEREO_ANAGLYPH_RC |
                                 PR_STEREO_ANAGLYPH_GM |
                                 PR_STEREO_ANAGLYPH_YB |
                                 PR_STEREO_ANAGLYPH_GRAY_RC |
                                 PR_STEREO_ANAGLYPH_GRAY_GM |
                                 PR_STEREO_ANAGLYPH_GRAY_YB |
                                 PR_STEREO_ANAGLYPH_DB_RC |
                                 PR_STEREO_ANAGLYPH_DB_GM |
                                 PR_STEREO_ANAGLYPH_DB_YB, /* anaglyph mask */

    PR_STEREO_MIRROR_LEFT      = 1 << 14, /* mirror */
    PR_STEREO_FLIP_LEFT        = 1 << 15, /* flip */

    PR_STEREO_MIRROR_RIGHT     = 1 << 16, /* mirror */
    PR_STEREO_FLIP_RIGHT       = 1 << 17, /* flip */

    PR_STEREO_MIRROR           = PR_STEREO_MIRROR_LEFT | PR_STEREO_MIRROR_RIGHT, /* mirror mask*/
    PR_STEREO_FLIP             = PR_STEREO_FLIP_LEFT | PR_STEREO_FLIP_RIGHT, /* flip mask*/
    PR_STEREO_SWAP             = 1 << 18,  /* swap left and right buffers */
    PR_STEREO_TEMP_DISABLE     = 1 << 19,  /* temporarily disable stereo mode if source image is not stereo */
    PR_STEREO_HALF             = 1 << 20
} PixbufRendererStereoMode;

typedef enum {
    STEREO_PIXBUF_DEFAULT  = 0,
    STEREO_PIXBUF_SBS      = 1,
    STEREO_PIXBUF_CROSS    = 2,
    STEREO_PIXBUF_NONE     = 3
} StereoPixbufData;

typedef struct ImageLoader ImageLoader;
typedef struct ThumbLoader ThumbLoader;

typedef struct CollectInfo CollectInfo;
typedef struct CollectionData CollectionData;
typedef struct CollectTable CollectTable;
typedef struct CollectWindow CollectWindow;

typedef struct ImageWindow ImageWindow;

typedef struct FileData FileData;
typedef struct FileDataChangeInfo FileDataChangeInfo;

typedef struct LayoutWindow LayoutWindow;
typedef struct LayoutOptions LayoutOptions;

typedef struct ViewDir ViewDir;
typedef struct ViewDirInfoList ViewDirInfoList;
typedef struct ViewDirInfoTree ViewDirInfoTree;

typedef struct ViewFile ViewFile;
typedef struct ViewFileInfoList ViewFileInfoList;
typedef struct ViewFileInfoIcon ViewFileInfoIcon;

typedef struct SlideShowData SlideShowData;
typedef struct FullScreenData FullScreenData;

typedef struct PixmapFolders PixmapFolders;
typedef struct HistMap HistMap;

typedef struct SecureSaveInfo SecureSaveInfo;

typedef struct ExifData ExifData;

typedef struct EditorDescription EditorDescription;

typedef struct CommandLine CommandLine;

typedef void (* ThumbLoaderFunc)(ThumbLoader *tl, gpointer data);

typedef void (* FileUtilDoneFunc)(gboolean success, const gchar *done_path, gpointer data);

typedef gint (* ImageTileRequestFunc)(ImageWindow *imd, gint x, gint y,
                                      gint width, gint height, GdkPixbuf *pixbuf, gpointer);
typedef void (* ImageTileDisposeFunc)(ImageWindow *imd, gint x, gint y,
                                      gint width, gint height, GdkPixbuf *pixbuf, gpointer);

typedef struct ViewFileFuncs ViewFileFuncs;

#endif
