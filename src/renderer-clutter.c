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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "main.h"
#include "pixbuf-renderer.h"
#include "renderer-clutter.h"

#include "intl.h"
#include "layout.h"

#include <gtk/gtk.h>

#ifdef HAVE_CLUTTER

/* for 3d texture */
#define COGL_ENABLE_EXPERIMENTAL_API
#define CLUTTER_ENABLE_EXPERIMENTAL_API

#include <clutter/clutter.h>

#include <clutter-gtk/clutter-gtk.h>



#define GQ_BUILD 1

#ifdef GQ_BUILD
#include "main.h"
#include "pixbuf_util.h"
#include "exif.h"
#else
typedef enum {
    EXIF_ORIENTATION_UNKNOWN    = 0,
    EXIF_ORIENTATION_TOP_LEFT   = 1,
    EXIF_ORIENTATION_TOP_RIGHT  = 2,
    EXIF_ORIENTATION_BOTTOM_RIGHT   = 3,
    EXIF_ORIENTATION_BOTTOM_LEFT    = 4,
    EXIF_ORIENTATION_LEFT_TOP   = 5,
    EXIF_ORIENTATION_RIGHT_TOP  = 6,
    EXIF_ORIENTATION_RIGHT_BOTTOM   = 7,
    EXIF_ORIENTATION_LEFT_BOTTOM    = 8
} ExifOrientationType;
#endif

#define GET_RIGHT_PIXBUF_OFFSET(rc) \
        (( (rc->stereo_mode & PR_STEREO_RIGHT) && !(rc->stereo_mode & PR_STEREO_SWAP)) || \
         (!(rc->stereo_mode & PR_STEREO_RIGHT) &&  (rc->stereo_mode & PR_STEREO_SWAP)) ?  \
          rc->pr->stereo_pixbuf_offset_right : rc->pr->stereo_pixbuf_offset_left )

#define GET_LEFT_PIXBUF_OFFSET(rc) \
        ((!(rc->stereo_mode & PR_STEREO_RIGHT) && !(rc->stereo_mode & PR_STEREO_SWAP)) || \
         ( (rc->stereo_mode & PR_STEREO_RIGHT) &&  (rc->stereo_mode & PR_STEREO_SWAP)) ?  \
          rc->pr->stereo_pixbuf_offset_right : rc->pr->stereo_pixbuf_offset_left )


typedef struct _OverlayData OverlayData;
struct _OverlayData
{
    gint id;

    GdkPixbuf *pixbuf;
    ClutterActor *actor;

    gint x;
    gint y;

    OverlayRendererFlags flags;
};

typedef struct _RendererClutter RendererClutter;

struct _RendererClutter
{
    RendererFuncs f;
    PixbufRenderer *pr;


    gint stereo_mode;
    gint stereo_off_x;
    gint stereo_off_y;


    GList *pending_updates;
    gint idle_update;

    GList *overlay_list;

    GtkWidget *widget; /* widget and stage may be shared with other renderers */
    ClutterActor *stage;
    ClutterActor *texture;
    ClutterActor *group;

    gboolean clut_updated;
    gint64 last_pixbuf_change;
};

typedef struct _RendererClutterAreaParam RendererClutterAreaParam;
struct _RendererClutterAreaParam {
    RendererClutter *rc;
    gint x;
    gint y;
    gint w;
    gint h;
};

typedef struct _RendererClutterShaderInfo RendererClutterShaderInfo;
struct _RendererClutterShaderInfo {
    float checkersize;
    float checkercolor0[3];
    float checkercolor1[3];
};

#define CLUT_SIZE   32

static void rc_set_shader(CoglHandle material, const RendererClutterShaderInfo *shaderInfo)
{
    CoglHandle shader;
    CoglHandle program;
    gint uniform_no;

    shader = cogl_create_shader (COGL_SHADER_TYPE_FRAGMENT);
    cogl_shader_source (shader,
    "vec3 checker(vec2 texc, vec3 color0, vec3 color1)                      \n"
    "{                                                                      \n"
    "  if (mod(floor(texc.x) + floor(texc.y), 2.0) == 0.0)                  \n"
    "    return color0;                                                     \n"
    "  else                                                                 \n"
    "    return color1;                                                     \n"
    "}                                                                      \n"
    "                                                                       \n"
    "uniform sampler2D tex;                                                 \n"
    "uniform sampler3D clut;                                                \n"
    "uniform float scale;                                                   \n"
    "uniform float offset;                                                  \n"
    "uniform float checkersize;                                             \n"
    "uniform vec3 color0;                                                   \n"
    "uniform vec3 color1;                                                   \n"
    "                                                                       \n"
    "void main(void)                                                        \n"
    "{                                                                      \n"
    "    vec3 bg = checker(gl_FragCoord.xy / checkersize, color0, color1);  \n"
    "    vec4 img4 = texture2D(tex, gl_TexCoord[0].xy);                     \n"
    "    vec3 img3 = img4.bgr;                                              \n"
    "    img3 = img3 * scale + offset;                                      \n"
    "    img3 = texture3D(clut, img3).rgb;                                  \n"
    "                                                                       \n"
    "    gl_FragColor = vec4(img3 * img4.a + bg * (1.0 - img4.a), 1.0);     \n"
    "}                                                                      \n"
    );
    cogl_shader_compile(shader);
    gchar *err = cogl_shader_get_info_log(shader);
    DEBUG_0("%s\n",err);
    g_free(err);

    program = cogl_create_program ();
    cogl_program_attach_shader (program, shader);
    cogl_handle_unref (shader);
    cogl_program_link (program);

    uniform_no = cogl_program_get_uniform_location (program, "tex");
    cogl_program_set_uniform_1i (program, uniform_no, 0);

    uniform_no = cogl_program_get_uniform_location (program, "clut");
    cogl_program_set_uniform_1i (program, uniform_no, 1);

    uniform_no = cogl_program_get_uniform_location (program, "scale");
    cogl_program_set_uniform_1f (program, uniform_no, (double) (CLUT_SIZE - 1) / CLUT_SIZE);

    uniform_no = cogl_program_get_uniform_location (program, "offset");
    cogl_program_set_uniform_1f (program, uniform_no, 1.0 / (2 * CLUT_SIZE));

    uniform_no = cogl_program_get_uniform_location (program, "checkersize");
    cogl_program_set_uniform_1f (program, uniform_no, shaderInfo->checkersize);

    uniform_no = cogl_program_get_uniform_location (program, "color0");
    cogl_program_set_uniform_float (program, uniform_no, 3, 1, shaderInfo->checkercolor0);

    uniform_no = cogl_program_get_uniform_location (program, "color1");
    cogl_program_set_uniform_float (program, uniform_no, 3, 1, shaderInfo->checkercolor1);

    cogl_material_set_user_program (material, program);
    cogl_handle_unref (program);
}


static void rc_prepare_post_process_lut(RendererClutter *rc)
{
    PixbufRenderer *pr = rc->pr;
    static guchar clut[CLUT_SIZE * CLUT_SIZE * CLUT_SIZE * 3];
    guint r, g, b;
    GdkPixbuf *tmp_pixbuf;
    CoglHandle material;
    CoglHandle tex3d;

    DEBUG_0("%s clut start", get_exec_time());

    for (r = 0; r < CLUT_SIZE; r++)
    {
        for (g = 0; g < CLUT_SIZE; g++)
        {
            for (b = 0; b < CLUT_SIZE; b++)
            {
                guchar *ptr = clut + ((b * CLUT_SIZE + g) * CLUT_SIZE + r) * 3;
                ptr[0] = floor ((double) r / (CLUT_SIZE - 1) * 255.0 + 0.5);
                ptr[1] = floor ((double) g / (CLUT_SIZE - 1) * 255.0 + 0.5);
                ptr[2] = floor ((double) b / (CLUT_SIZE - 1) * 255.0 + 0.5);
            }
        }
    }
    tmp_pixbuf = gdk_pixbuf_new_from_data(clut, GDK_COLORSPACE_RGB, FALSE, 8,
                          CLUT_SIZE * CLUT_SIZE,
                          CLUT_SIZE,
                          CLUT_SIZE * CLUT_SIZE * 3,
                          NULL, NULL);
    if (pr->func_post_process)
    {
        pr->func_post_process(pr, &tmp_pixbuf, 0, 0, CLUT_SIZE * CLUT_SIZE, CLUT_SIZE, pr->post_process_user_data);
    }
    g_object_unref(tmp_pixbuf);

    DEBUG_0("%s clut upload start", get_exec_time());
#if COGL_VERSION_CHECK(1,18,2)
    {
    CoglContext *ctx = clutter_backend_get_cogl_context(clutter_get_default_backend ());

    tex3d = cogl_texture_3d_new_from_data(ctx,
                          CLUT_SIZE, CLUT_SIZE, CLUT_SIZE,
                          COGL_PIXEL_FORMAT_RGB_888,
                          CLUT_SIZE * 3,
                          CLUT_SIZE * CLUT_SIZE * 3,
                          clut,
                          NULL);
    }
#elif COGL_VERSION_CHECK(1,10,4)
    {
    CoglContext *ctx = clutter_backend_get_cogl_context(clutter_get_default_backend ());

    tex3d = cogl_texture_3d_new_from_data(ctx,
                          CLUT_SIZE, CLUT_SIZE, CLUT_SIZE,
                          COGL_PIXEL_FORMAT_RGB_888,
                          COGL_PIXEL_FORMAT_RGB_888,
                          CLUT_SIZE * 3,
                          CLUT_SIZE * CLUT_SIZE * 3,
                          clut,
                          NULL);
    }
#else
    tex3d = cogl_texture_3d_new_from_data(CLUT_SIZE, CLUT_SIZE, CLUT_SIZE,
                          COGL_TEXTURE_NONE,
                          COGL_PIXEL_FORMAT_RGB_888,
                          COGL_PIXEL_FORMAT_RGB_888,
                          CLUT_SIZE * 3,
                          CLUT_SIZE * CLUT_SIZE * 3,
                          clut,
                          NULL);
#endif
    material = clutter_texture_get_cogl_material(CLUTTER_TEXTURE(rc->texture));
    cogl_material_set_layer(material, 1, tex3d);
    cogl_handle_unref(tex3d);
    DEBUG_0("%s clut end", get_exec_time());
    rc->clut_updated = TRUE;
}



static void rc_sync_actor(RendererClutter *rc)
{
    PixbufRenderer *pr = rc->pr;
    gint anchor_x = 0;
    gint anchor_y = 0;
    gint rot_z = 0;
    gint rot_y = 0;

    clutter_actor_set_anchor_point(CLUTTER_ACTOR(rc->texture), 0, 0);

    DEBUG_0("scale %d %d", rc->pr->width, rc->pr->height);
    DEBUG_0("pos   %d %d", rc->pr->x_offset, rc->pr->y_offset);

    clutter_actor_set_scale(CLUTTER_ACTOR(rc->texture),
                    (gfloat)pr->width / pr->image_width,
                    (gfloat)pr->height / pr->image_height);

    switch (pr->orientation)
    {
        case EXIF_ORIENTATION_TOP_LEFT:
            /* 1 - Horizontal (normal)  */
            break;
        case EXIF_ORIENTATION_TOP_RIGHT:
            /* 2 - Mirror horizontal */
            rot_y = 180;
            anchor_x = pr->width;
            break;
        case EXIF_ORIENTATION_BOTTOM_RIGHT:
            /* 3 - Rotate 180 */
            rot_z = 180;
            anchor_x = pr->width;
            anchor_y = pr->height;
            break;
        case EXIF_ORIENTATION_BOTTOM_LEFT:
            /* 4 - Mirror vertical */
            rot_z = 180;
            rot_y = 180;
            anchor_y = pr->height;
            break;
        case EXIF_ORIENTATION_LEFT_TOP:
            /* 5 - Mirror horizontal and rotate 270 CW */
            rot_z = 270;
            rot_y = 180;
            break;
        case EXIF_ORIENTATION_RIGHT_TOP:
            /* 6 - Rotate 90 CW */
            rot_z = 90;
            anchor_x = pr->width;
            break;
        case EXIF_ORIENTATION_RIGHT_BOTTOM:
            /* 7 - Mirror horizontal and rotate 90 CW */
            rot_z = 90;
            rot_y = 180;
            anchor_x = pr->width;
            anchor_y = pr->height;
            break;
        case EXIF_ORIENTATION_LEFT_BOTTOM:
            /* 8 - Rotate 270 CW */
            rot_z = 270;
            anchor_y = pr->height;
            break;
        default:
            /* The other values are out of range */
            break;
    }

    clutter_actor_set_rotation( CLUTTER_ACTOR(rc->texture),
                                CLUTTER_Z_AXIS,
                                rot_z, 0, 0, 0);
    clutter_actor_set_rotation( CLUTTER_ACTOR(rc->texture),
                                CLUTTER_Y_AXIS,
                                rot_y, 0, 0, 0);

    clutter_actor_set_position(CLUTTER_ACTOR(rc->texture),
                pr->x_offset - pr->x_scroll + anchor_x,
                pr->y_offset - pr->y_scroll + anchor_y);

}


static void rc_area_clip_add(RendererClutter *rc, gfloat x, gfloat y, gfloat w, gfloat h)
{
    gfloat x2, y2;
    gfloat clip_x, clip_y, clip_w, clip_h, clip_x2, clip_y2;

    x2 = x + w;
    y2 = y + h;

    clutter_actor_get_clip(rc->texture, &clip_x, &clip_y, &clip_w, &clip_h);

    clip_x2 = clip_x + clip_w;
    clip_y2 = clip_y + clip_h;

    if (clip_x > x) clip_x = x;
    if (clip_x2 < x2) clip_x2 = x2;
    if (clip_y > y) clip_y = y;
    if (clip_y2 < y2) clip_y2 = y2;

    clip_w = clip_x2 - clip_x;
    clip_h = clip_y2 - clip_y;

    clutter_actor_set_clip(rc->texture, clip_x, clip_y, clip_w, clip_h);
}

#define MAX_REGION_AREA (32768 * 1024)

static gboolean rc_area_changed_cb(gpointer data);

static void rc_schedule_texture_upload(RendererClutter *rc)
{
    if (g_get_monotonic_time() - rc->last_pixbuf_change < 50000)
    {
        /* delay clutter redraw until the texture has some data
           set priority between gtk redraw and clutter redraw */
        DEBUG_0("%s tex upload high prio", get_exec_time());
        rc->idle_update = g_idle_add_full(CLUTTER_PRIORITY_REDRAW - 10, rc_area_changed_cb, rc, NULL);
    }
    else
    {
        /* higher prio than histogram */
        DEBUG_0("%s tex upload low prio", get_exec_time());

        rc->idle_update = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE - 5, rc_area_changed_cb, rc, NULL);
    }
}

static gboolean rc_area_changed_cb(gpointer data)
{
    RendererClutter *rc = (RendererClutter *)data;
    PixbufRenderer *pr = rc->pr;

    RendererClutterAreaParam *par = rc->pending_updates->data;

    gint h = MAX_REGION_AREA / par->w;
    if (h == 0) h = 1;
    if (h > par->h) h = par->h;


    DEBUG_0("%s upload start", get_exec_time());
    if (pr->pixbuf)
    {
        CoglHandle texture = clutter_texture_get_cogl_texture(CLUTTER_TEXTURE(rc->texture));

        cogl_texture_set_region(texture,
                    par->x + GET_RIGHT_PIXBUF_OFFSET(rc),
                    par->y,
                    par->x,
                    par->y,
                    par->w,
                    h,
                    par->w,
                    h,
                    gdk_pixbuf_get_has_alpha(pr->pixbuf) ? COGL_PIXEL_FORMAT_BGRA_8888 : COGL_PIXEL_FORMAT_BGR_888,
                    gdk_pixbuf_get_rowstride(pr->pixbuf),
                    gdk_pixbuf_get_pixels(pr->pixbuf));
    }
    DEBUG_0("%s upload end", get_exec_time());
    rc_area_clip_add(rc, par->x, par->y, par->w, h);


    par->y += h;
    par->h -= h;

    if (par->h == 0)
    {
        rc->pending_updates = g_list_remove(rc->pending_updates, par);
        g_free(par);
    }
    if (!rc->pending_updates)
    {
        clutter_actor_queue_redraw(CLUTTER_ACTOR(rc->texture));
        rc->idle_update = 0;

        /* FIXME: find a better place for this */
        if (!rc->clut_updated) rc_prepare_post_process_lut(rc);

        return FALSE;
    }

    rc_schedule_texture_upload(rc);

    return FALSE; /* it was rescheduled, possibly with different prio */
}


static void rc_area_changed(void *renderer, gint src_x, gint src_y, gint src_w, gint src_h)
{
    RendererClutter *rc = (RendererClutter *)renderer;
    PixbufRenderer *pr = rc->pr;
    RendererClutterAreaParam *par;

    gint width = gdk_pixbuf_get_width(pr->pixbuf);
    gint height = gdk_pixbuf_get_height(pr->pixbuf);

    if (pr->stereo_data == STEREO_PIXBUF_SBS || pr->stereo_data == STEREO_PIXBUF_CROSS)
        {
            width /= 2;
        }

    if (!pr_clip_region(src_x, src_y, src_w, src_h,
                        GET_RIGHT_PIXBUF_OFFSET(rc), 0, width, height,
                        &src_x, &src_y, &src_w, &src_h)) return;

    par = g_new0(RendererClutterAreaParam, 1);
    par->rc = rc;
    par->x = src_x - GET_RIGHT_PIXBUF_OFFSET(rc);
    par->y = src_y;
    par->w = src_w;
    par->h = src_h;
    rc->pending_updates = g_list_append(rc->pending_updates, par);
    if (!rc->idle_update)
    {
        rc_schedule_texture_upload(rc);
    }
}

static void rc_remove_pending_updates(RendererClutter *rc)
{
    if (rc->idle_update) g_idle_remove_by_data(rc);
    rc->idle_update = 0;
    while (rc->pending_updates)
    {
        RendererClutterAreaParam *par = rc->pending_updates->data;
        rc->pending_updates = g_list_remove(rc->pending_updates, par);
        g_free(par);
    }
}

static void rc_update_pixbuf(void *renderer, gboolean lazy)
{
    RendererClutter *rc = (RendererClutter *)renderer;
    PixbufRenderer *pr = rc->pr;

    DEBUG_0("rc_update_pixbuf");

    rc_remove_pending_updates(rc);

    rc->last_pixbuf_change = g_get_monotonic_time();
    DEBUG_0("%s change time reset", get_exec_time());

    if (pr->pixbuf)
    {
        gint width = gdk_pixbuf_get_width(pr->pixbuf);
        gint height = gdk_pixbuf_get_height(pr->pixbuf);

        DEBUG_0("pixbuf size %d x %d (%d)", width, height, gdk_pixbuf_get_has_alpha(pr->pixbuf) ? 32 : 24);

        gint prev_width, prev_height;

        if (pr->stereo_data == STEREO_PIXBUF_SBS || pr->stereo_data == STEREO_PIXBUF_CROSS)
        {
            width /= 2;
        }


        clutter_texture_get_base_size(CLUTTER_TEXTURE(rc->texture), &prev_width, &prev_height);

        if (width != prev_width || height != prev_height)
        {
            /* FIXME use CoglMaterial with multiple textures for background, color management, anaglyph, ... */
            CoglHandle texture = cogl_texture_new_with_size(width,
                                    height,
                                    COGL_TEXTURE_NO_AUTO_MIPMAP | COGL_TEXTURE_NO_SLICING,
                                    gdk_pixbuf_get_has_alpha(pr->pixbuf) ? COGL_PIXEL_FORMAT_BGRA_8888 : COGL_PIXEL_FORMAT_BGR_888);

            if (texture != COGL_INVALID_HANDLE)
            {
                clutter_texture_set_cogl_texture(CLUTTER_TEXTURE(rc->texture), texture);
                cogl_handle_unref(texture);
            }
        }
        clutter_actor_set_clip(rc->texture, 0, 0, 0, 0); /* visible area is extended as area_changed events arrive */
        if (!lazy)
        {
            rc_area_changed(renderer, GET_RIGHT_PIXBUF_OFFSET(rc), 0, width, height);
        }
    }

    rc->clut_updated = FALSE;
}



static void rc_update_zoom(void *renderer, gboolean lazy)
{
    RendererClutter *rc = (RendererClutter *)renderer;

    DEBUG_0("rc_update_zoom");
    rc_sync_actor(rc);
}

static void rc_invalidate_region(void *renderer, gint x, gint y, gint w, gint h)
{
}

static OverlayData *rc_overlay_find(RendererClutter *rc, gint id)
{
    GList *work;

    work = rc->overlay_list;
    while (work)
    {
        OverlayData *od = work->data;
        work = work->next;

        if (od->id == id) return od;
    }

    return NULL;
}

static void rc_overlay_actor_destroy_cb(ClutterActor *actor, gpointer user_data)
{
    OverlayData *od = user_data;
    od->actor = NULL;
}

static void rc_overlay_free(RendererClutter *rc, OverlayData *od)
{
    rc->overlay_list = g_list_remove(rc->overlay_list, od);

    if (od->pixbuf) g_object_unref(G_OBJECT(od->pixbuf));
    if (od->actor) clutter_actor_destroy(od->actor);
    g_free(od);
}

static void rc_overlay_update_position(RendererClutter *rc, OverlayData *od)
{
    gint px, py, pw, ph;

    pw = gdk_pixbuf_get_width(od->pixbuf);
    ph = gdk_pixbuf_get_height(od->pixbuf);
    px = od->x;
    py = od->y;

    if (od->flags & OVL_RELATIVE)
    {
        if (px < 0) px = rc->pr->viewport_width - pw + px;
        if (py < 0) py = rc->pr->viewport_height - ph + py;
    }
    if (od->actor) clutter_actor_set_position(od->actor, px, py);
}

static void rc_overlay_update_positions(RendererClutter *rc)
{
    GList *work;

    work = rc->overlay_list;
    while (work)
    {
        OverlayData *od = work->data;
        work = work->next;

        rc_overlay_update_position(rc, od);
    }
}

static void rc_overlay_free_all(RendererClutter *rc)
{
    GList *work;

    work = rc->overlay_list;
    while (work)
    {
        OverlayData *od = work->data;
        work = work->next;

        rc_overlay_free(rc, od);
    }
}

static gint rc_overlay_add(void *renderer, GdkPixbuf *pixbuf, gint x, gint y, OverlayRendererFlags flags)
{
    RendererClutter *rc = (RendererClutter *)renderer;
    PixbufRenderer *pr = rc->pr;
    OverlayData *od;
    gint id;

    g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), -1);
    g_return_val_if_fail(pixbuf != NULL, -1);

    id = 1;
    while (rc_overlay_find(rc, id)) id++;

    od = g_new0(OverlayData, 1);
    od->id = id;
    od->pixbuf = pixbuf;
    g_object_ref(G_OBJECT(od->pixbuf));
    od->x = x;
    od->y = y;
    od->flags = flags;

    od->actor = gtk_clutter_texture_new();
    g_signal_connect (od->actor, "destroy", G_CALLBACK(rc_overlay_actor_destroy_cb), od);

    gtk_clutter_texture_set_from_pixbuf(GTK_CLUTTER_TEXTURE (od->actor), pixbuf, NULL);
    clutter_container_add_actor(CLUTTER_CONTAINER(rc->group), od->actor);

    rc->overlay_list = g_list_append(rc->overlay_list, od);
    rc_overlay_update_position(rc, od);

    return od->id;
}

static void rc_overlay_set(void *renderer, gint id, GdkPixbuf *pixbuf, gint x, gint y)
{
    RendererClutter *rc = (RendererClutter *)renderer;
    PixbufRenderer *pr = rc->pr;
    OverlayData *od;

    g_return_if_fail(IS_PIXBUF_RENDERER(pr));

    od = rc_overlay_find(rc, id);
    if (!od) return;

    if (pixbuf)
    {
        g_object_ref(G_OBJECT(pixbuf));
        g_object_unref(G_OBJECT(od->pixbuf));
        od->pixbuf = pixbuf;

        od->x = x;
        od->y = y;

        if (od->actor) gtk_clutter_texture_set_from_pixbuf(GTK_CLUTTER_TEXTURE(od->actor), pixbuf, NULL);
        rc_overlay_update_position(rc, od);
    }
    else
    {
        rc_overlay_free(rc, od);
    }
}

static gboolean rc_overlay_get(void *renderer, gint id, GdkPixbuf **pixbuf, gint *x, gint *y)
{
    RendererClutter *rc = (RendererClutter *)renderer;

    PixbufRenderer *pr = rc->pr;
    OverlayData *od;

    g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);

    od = rc_overlay_find(rc, id);
    if (!od) return FALSE;

    if (pixbuf) *pixbuf = od->pixbuf;
    if (x) *x = od->x;
    if (y) *y = od->y;

    return TRUE;
}


static void rc_update_viewport(void *renderer)
{
    RendererClutter *rc = (RendererClutter *)renderer;
    ClutterColor stage_color = { 0x00, 0x00, 0x00, 0xff };

    rc->stereo_off_x = 0;
    rc->stereo_off_y = 0;

    if (rc->stereo_mode & PR_STEREO_RIGHT)
    {
        if (rc->stereo_mode & PR_STEREO_HORIZ)
        {
            rc->stereo_off_x = rc->pr->viewport_width;
        }
        else if (rc->stereo_mode & PR_STEREO_VERT)
        {
            rc->stereo_off_y = rc->pr->viewport_height;
        }
        else if (rc->stereo_mode & PR_STEREO_FIXED)
        {
            rc->stereo_off_x = rc->pr->stereo_fixed_x_right;
            rc->stereo_off_y = rc->pr->stereo_fixed_y_right;
        }
    }
    else
    {
        if (rc->stereo_mode & PR_STEREO_FIXED)
        {
            rc->stereo_off_x = rc->pr->stereo_fixed_x_left;
            rc->stereo_off_y = rc->pr->stereo_fixed_y_left;
        }
    }
    DEBUG_0("rc_update_viewport  scale %d %d", rc->pr->width, rc->pr->height);

    clutter_stage_set_color(CLUTTER_STAGE(rc->stage), &stage_color);


    clutter_actor_set_size(rc->group, rc->pr->viewport_width, rc->pr->viewport_height);
    clutter_actor_set_position(rc->group, rc->stereo_off_x, rc->stereo_off_y);

    clutter_actor_set_rotation(CLUTTER_ACTOR(rc->group),
                        CLUTTER_Y_AXIS,
                        (rc->stereo_mode & PR_STEREO_MIRROR) ? 180 : 0,
                        rc->pr->viewport_width / 2.0, 0, 0);

    clutter_actor_set_rotation(CLUTTER_ACTOR(rc->group),
                        CLUTTER_X_AXIS,
                        (rc->stereo_mode & PR_STEREO_FLIP) ? 180 : 0,
                        0, rc->pr->viewport_height / 2.0, 0);

    rc_sync_actor(rc);
    rc_overlay_update_positions(rc);
}

static void rc_scroll(void *renderer, gint x_off, gint y_off)
{
    DEBUG_0("rc_scroll");
    RendererClutter *rc = (RendererClutter *)renderer;

    rc_sync_actor(rc);
}

static void rc_stereo_set(void *renderer, gint stereo_mode)
{
    RendererClutter *rc = (RendererClutter *)renderer;

    rc->stereo_mode = stereo_mode;
}

static void rc_free(void *renderer)
{
    RendererClutter *rc = (RendererClutter *)renderer;
    GtkWidget *widget = gtk_bin_get_child(GTK_BIN(rc->pr));

    rc_remove_pending_updates(rc);

    rc_overlay_free_all(rc);

    if (widget)
    {
        /* widget still exists */
        clutter_actor_destroy(rc->group);
        if (clutter_group_get_n_children(CLUTTER_GROUP(rc->stage)) == 0)
        {
            DEBUG_1("destroy %p", rc->widget);
            /* this was the last user */
            gtk_widget_destroy(rc->widget);
        }
        else
        {
            DEBUG_1("keep %p", rc->widget);
            g_object_unref(G_OBJECT(rc->widget));
        }
    }
    g_free(rc);
}

/* initialize shader for transparency background checker */
static void renderer_clutter_init_checker_shader(RendererClutter *rc)
{
    const RendererClutterShaderInfo info = {
        16.0,               /* checker size */
    {0.6, 0.6, 0.6},    /* color 0 */
    {0.4, 0.4, 0.4}     /* color 1 */
    };
    rc_set_shader(clutter_texture_get_cogl_material(CLUTTER_TEXTURE(rc->texture)), &info);
}

RendererFuncs *renderer_clutter_new(PixbufRenderer *pr)
{
    RendererClutter *rc = g_new0(RendererClutter, 1);

    rc->pr = pr;

    rc->f.area_changed = rc_area_changed;
    rc->f.update_pixbuf = rc_update_pixbuf;
    rc->f.free = rc_free;
    rc->f.update_zoom = rc_update_zoom;
    rc->f.invalidate_region = rc_invalidate_region;
    rc->f.scroll = rc_scroll;
    rc->f.update_viewport = rc_update_viewport;


    rc->f.overlay_add = rc_overlay_add;
    rc->f.overlay_set = rc_overlay_set;
    rc->f.overlay_get = rc_overlay_get;

    rc->f.stereo_set = rc_stereo_set;


    rc->stereo_mode = 0;
    rc->stereo_off_x = 0;
    rc->stereo_off_y = 0;

    rc->idle_update = 0;
    rc->pending_updates = NULL;

    rc->widget = gtk_bin_get_child(GTK_BIN(rc->pr));

    if (rc->widget)
    {
        if (!GTK_CLUTTER_IS_EMBED(rc->widget))
        {
            g_free(rc);
            DEBUG_0("pixbuf renderer has a child of other type than gtk_clutter_embed");
            return NULL;
        }
    }
    else
    {
        rc->widget = gtk_clutter_embed_new();
        gtk_container_add(GTK_CONTAINER(rc->pr), rc->widget);
    }

    gtk_event_box_set_above_child (GTK_EVENT_BOX(rc->pr), TRUE);
    rc->stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (rc->widget));

    rc->group = clutter_group_new();
    clutter_container_add_actor(CLUTTER_CONTAINER(rc->stage), rc->group);
    clutter_actor_set_clip_to_allocation(CLUTTER_ACTOR(rc->group), TRUE);

    rc->texture = clutter_texture_new ();
    clutter_container_add_actor(CLUTTER_CONTAINER(rc->group), rc->texture);

    renderer_clutter_init_checker_shader(rc);
    g_object_ref(G_OBJECT(rc->widget));

    gtk_widget_show(rc->widget);
    return (RendererFuncs *) rc;
}

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
