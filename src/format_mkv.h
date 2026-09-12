#ifndef FORMAT_MKV_H
#define FORMAT_MKV_H

#ifdef __cplusplus
extern "C" {
#endif

/* Locate an image/ attachment (e.g. a cover image) embedded in a Matroska
 * (.mkv/.webm) file's Attachments element, without copying its payload.
 *
 * requested_index selects which image/* attachment (0-based, in the order
 * they appear in the file) to return; total_images_out, if non-NULL, always
 * receives the total number of image/* attachments found, even on failure.
 *
 * On success, returns TRUE and:
 *   - mmap_base_out / mmap_base_len_out receive the whole-file mmap() region
 *     that must later be munmap()'d with this exact (pointer, length) pair.
 *   - image_data_out / image_len_out receive a pointer into that region
 *     (mmap_base + some offset) and length, ready to hand to a pixbuf loader.
 * On failure, returns FALSE and does not touch any of the output parameters
 * (any mmap() performed internally on a failure path is undone before
 * returning).
 */
gboolean mkv_get_image_region(const gchar *path, guint index,
                               guchar **mmap_base_out, gsize *mmap_base_len_out,
                               guchar **image_data_out, gsize *image_len_out,
                               guint *total_images_out);
#ifdef __cplusplus
}
#endif

#endif
