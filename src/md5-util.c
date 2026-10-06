/*
 * Copyright (C) 1993 Branko Lankester
 * Copyright (C) 1993 Colin Plumb
 * Copyright (C) 1995 Erik Troan
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
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
 *
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 */

#include <stdio.h>
#include <string.h>
#include "md5-util.h"

#define MD5_SIZE 16

static gboolean md5_update_from_file(GChecksum *md5, const gchar *path)
{
    guchar tmp_buf[1024];
    gint nb_bytes_read;

    FILE *fp = fopen(path, "r");
    if (!fp) return FALSE;

    while ((nb_bytes_read = fread(tmp_buf, sizeof (guchar), sizeof(tmp_buf), fp)) > 0)
        g_checksum_update(md5, tmp_buf, nb_bytes_read);

    gint success = (ferror(fp) == 0);
    fclose(fp);

    return success;
}

gchar *md5_get_string(const guchar *buffer, gint buffer_size)
{
    GChecksum *md5 = g_checksum_new(G_CHECKSUM_MD5);

    if (!md5) return NULL;

    g_checksum_update(md5, buffer, buffer_size);
    gchar *result = g_strdup(g_checksum_get_string(md5));
    g_checksum_free(md5);

    return result;
}

gboolean md5_get_digest_from_file(const gchar *path, guchar digest[16])
{
    GChecksum *md5 = g_checksum_new(G_CHECKSUM_MD5);

    if (!md5) return FALSE;

    if (!md5_update_from_file(md5, path))
    {
        g_checksum_free(md5);
        return FALSE;
    }

    gsize digest_size = MD5_SIZE;
    g_checksum_get_digest(md5, digest, &digest_size);
    g_checksum_free(md5);
    return digest_size == MD5_SIZE;
}

gchar *md5_get_string_from_file(const gchar *path)
{
    GChecksum *md5 = g_checksum_new(G_CHECKSUM_MD5);

    if (!md5) return NULL;

    if (!md5_update_from_file(md5, path))
    {
        g_checksum_free(md5);
        return NULL;
    }

    gchar *result = g_strdup(g_checksum_get_string(md5));
    g_checksum_free(md5);

    return result;
}

gchar *md5_digest_to_text(guchar digest[16])
{
    static gchar hex_digits[] = "0123456789abcdef";
    gchar *result;
    gsize result_size = 2 * MD5_SIZE;

    result = g_malloc(result_size + 1);
    for (gsize i = 0; i < MD5_SIZE; i++)
    {
        result[2*i]   = hex_digits[digest[i] >> 4];
        result[2*i+1] = hex_digits[digest[i] & 0xf];
    }
    result[result_size] = '\0';

    return result;
}

gboolean md5_digest_from_text(const gchar *text, guchar digest[16])
{
    for (gsize i = 0; i < MD5_SIZE; i++)
    {
        if (text[2*i] == '\0' || text[2*i+1] == '\0') return FALSE;
        digest[i] = g_ascii_xdigit_value(text[2*i]) << 4 |
                    g_ascii_xdigit_value(text[2*i + 1]);
    }

    return TRUE;
}

