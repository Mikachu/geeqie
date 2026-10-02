/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Eric Swalens, Quy Tonthat, John Ellis
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

#ifndef __EXIF_H
#define __EXIF_H

#include "metadata.h"

#define EXIF_FORMATTED() "formatted."
#define EXIF_FORMATTED_LEN (sizeof(EXIF_FORMATTED()) - 1)

/*
 *-----------------------------------------------------------------------------
 * Tag formats
 *-----------------------------------------------------------------------------
 */

#define EXIF_FORMAT_COUNT 13

typedef enum {
    EXIF_FORMAT_UNKNOWN             = 0,
    EXIF_FORMAT_BYTE_UNSIGNED       = 1,
    EXIF_FORMAT_STRING              = 2,
    EXIF_FORMAT_SHORT_UNSIGNED      = 3,
    EXIF_FORMAT_LONG_UNSIGNED       = 4,
    EXIF_FORMAT_RATIONAL_UNSIGNED   = 5,
    EXIF_FORMAT_BYTE                = 6,
    EXIF_FORMAT_UNDEFINED           = 7,
    EXIF_FORMAT_SHORT               = 8,
    EXIF_FORMAT_LONG                = 9,
    EXIF_FORMAT_RATIONAL            = 10,
    EXIF_FORMAT_FLOAT               = 11,
    EXIF_FORMAT_DOUBLE              = 12
} ExifFormatType;

/*
 *-----------------------------------------------------------------------------
 * Data storage
 *-----------------------------------------------------------------------------
 */

typedef struct ExifData ExifData;
typedef struct ExifItem ExifItem;

typedef struct ExifRational ExifRational;
struct ExifRational
{
    guint32 num;
    guint32 den;
};

/* enums useful for image manipulation */

typedef enum {
    EXIF_ORIENTATION_UNKNOWN        = 0,
    EXIF_ORIENTATION_TOP_LEFT       = 1,
    EXIF_ORIENTATION_TOP_RIGHT      = 2,
    EXIF_ORIENTATION_BOTTOM_RIGHT   = 3,
    EXIF_ORIENTATION_BOTTOM_LEFT    = 4,
    EXIF_ORIENTATION_LEFT_TOP       = 5,
    EXIF_ORIENTATION_RIGHT_TOP      = 6,
    EXIF_ORIENTATION_RIGHT_BOTTOM   = 7,
    EXIF_ORIENTATION_LEFT_BOTTOM    = 8
} ExifOrientationType;

typedef enum {
    EXIF_UNIT_UNKNOWN       = 0,
    EXIF_UNIT_NOUNIT        = 1,
    EXIF_UNIT_INCH          = 2,
    EXIF_UNIT_CENTIMETER    = 3
} ExifUnitType;

struct ExifData;

typedef struct ExifFormattedText ExifFormattedText;
struct ExifFormattedText
{
    const gchar *key;
    const gchar *description;
    gchar *(*build_func)(struct ExifData *exif);
};

/*
 *-----------------------------------------------------------------------------
 * functions
 *-----------------------------------------------------------------------------
 */

void exif_init(void);

struct ExifData *exif_read(gchar *path, gchar *sidecar_path, GHashTable *modified_xmp);

struct FileData;

struct ExifData *exif_read_fd(struct FileData *fd);
void exif_free_fd(struct FileData *fd, struct ExifData *exif);

/* exif_read returns processed data (merged from image and sidecar, etc.)
   this function gives access to the original data from the image.
   original data are part of the processed data and should not be freed separately */
struct ExifData *exif_get_original(struct ExifData *processed);

gboolean exif_write(struct ExifData *exif);
gboolean exif_write_sidecar(struct ExifData *exif, gchar *path);

void exif_free(struct ExifData *exif);

gchar *exif_get_data_as_text(struct ExifData *exif, const gchar *key);
gint exif_get_integer(struct ExifData *exif, const gchar *key, gint *value);
ExifRational *exif_get_rational(struct ExifData *exif, const gchar *key, gint *sign);

ExifItem *exif_get_item(struct ExifData *exif, const gchar *key);
ExifItem *exif_get_first_item(struct ExifData *exif);
ExifItem *exif_get_next_item(struct ExifData *exif);

gchar *exif_item_get_tag_name(ExifItem *item);
guint exif_item_get_tag_id(ExifItem *item);
guint exif_item_get_elements(ExifItem *item);
gchar *exif_item_get_data(ExifItem *item, guint *data_len);
gchar *exif_item_get_description(ExifItem *item);
guint exif_item_get_format_id(ExifItem *item);
const gchar *exif_item_get_format_name(ExifItem *item, gboolean brief);
gchar *exif_item_get_data_as_text(ExifItem *item, struct ExifData *exif);
gint exif_item_get_integer(ExifItem *item, gint *value);
ExifRational *exif_item_get_rational(ExifItem *item, gint *sign, guint n);

gchar *exif_item_get_string(ExifItem *item, gint idx);

gchar *exif_get_description_by_key(const gchar *key);
gchar *exif_get_tag_description_by_key(const gchar *key);

gchar *exif_get_formatted_by_key(struct ExifData *exif, const gchar *key, gboolean *key_valid);

gint exif_update_metadata(struct ExifData *exif, const gchar *key, const GList *values);
GList *exif_get_metadata(struct ExifData *exif, const gchar *key, MetadataFormat format);

guchar *exif_get_color_profile(struct ExifData *exif, guint *data_len);

/* jpeg embedded icc support */

void exif_add_jpeg_color_profile(struct ExifData *exif, guchar *cp_data, guint cp_length);

gboolean exif_jpeg_parse_color(struct ExifData *exif, guchar *data, guint size);

/*raw support */
guchar *exif_get_preview(struct ExifData *exif, guint *data_len, gint requested_width, gint requested_height);
void exif_free_preview(guchar *buf);

gchar *metadata_file_info(struct FileData *fd, const gchar *key, MetadataFormat format);

#endif
