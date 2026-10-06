/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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
#include "misc.h"

gdouble get_zoom_increment(void)
{
    return ((options->image.zoom_increment != 0)
            ? (gdouble)options->image.zoom_increment / 100.0
            : 1.0);
}

/* XXX very questionable function */
gchar *utf8_validate_or_convert(const gchar *text)
{
    if (!text) return NULL;

    gint len = strlen(text);
    if (!g_utf8_validate(text, len, NULL))
        return g_convert(text, len, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);

    return g_strdup(text);
}

gint utf8_compare(const gchar *s1, const gchar *s2, gboolean case_sensitive)
{
    gchar *s1_key, *s2_key;
    gchar *s1_t, *s2_t;
    gint ret;

    g_assert(g_utf8_validate(s1, -1, NULL));
    g_assert(g_utf8_validate(s2, -1, NULL));

    if (!case_sensitive)
    {
        s1_t = g_utf8_casefold(s1, -1);
        s2_t = g_utf8_casefold(s2, -1);
    }
    else
    {
        s1_t = (gchar *) s1;
        s2_t = (gchar *) s2;
    }

    s1_key = g_utf8_collate_key(s1_t, -1);
    s2_key = g_utf8_collate_key(s2_t, -1);

    ret = strcmp(s1_key, s2_key);

    g_free(s1_key);
    g_free(s2_key);

    if (!case_sensitive)
    {
        g_free(s1_t);
        g_free(s2_t);
    }

    return ret;
}

/* Run a command like system() but may output debug messages. */
int runcmd(gchar *cmd)
{
#if 1
    return printf("Would have run: %s\n", cmd);
    return 0;
#else
    /* For debugging purposes */
    int retval = -1;
    FILE *in;

    DEBUG_1("Running command: %s", cmd);

    in = popen(cmd, "r");
    if (in)
    {
        int status;
        const gchar *msg;
        gchar buf[2048];

        while (fgets(buf, sizeof(buf), in) != NULL )
        {
            DEBUG_1("Output: %s", buf);
        }

        status = pclose(in);

        if (WIFEXITED(status))
        {
            msg = "Command terminated with exit code";
            retval = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            msg = "Command was killed by signal";
            retval = WTERMSIG(status);
        }
        else
        {
            msg = "pclose() returned";
            retval = status;
        }

        DEBUG_1("%s : %d\n", msg, retval);
    }

    return retval;
#endif
}


