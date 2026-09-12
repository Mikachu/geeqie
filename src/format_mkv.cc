#include <glib.h>
#ifdef HAVE_CONFIG_H
#  include "config.h"
#else
#  define HAVE_MATROSKA
#endif
#include "format_mkv.h"

#ifdef HAVE_MATROSKA

#include <ebml/EbmlHead.h>
#include <ebml/EbmlStream.h>
#include <ebml/EbmlSubHead.h>
#include <ebml/EbmlVoid.h>
#include <ebml/StdIOCallback.h>
#include <matroska/KaxSegment.h>
#include <matroska/KaxSemantic.h>
#include <matroska/KaxSeekHead.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cassert>
#include <string>
#include <cerrno>
#include <stdexcept>

using namespace libebml;
using namespace libmatroska;

/* we inconveniently have to reimplement this whole class just to get libebml
 * to not re-open the file we already have an fd for, great */
class FdIOCallback : public IOCallback
{
public:
    explicit FdIOCallback(int fd) : mFd(fd), mCurrentPosition(0) {}
    ~FdIOCallback() override { close(); }

    uint32 read(void *Buffer, size_t Size) override
    {
        size_t total = 0;
        while (total < Size) {
            ssize_t n = ::read(mFd, (char*)Buffer + total, Size - total);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (n == 0) break; // real EOF
            total += (size_t)n;
        }
        mCurrentPosition += total;
        return (uint32)total;
    }

    void setFilePointer(int64 Offset, seek_mode Mode) override
    {
        assert(Mode == SEEK_CUR || Mode == SEEK_END || Mode == SEEK_SET);
        off_t newPos = lseek(mFd, (off_t)Offset, Mode);
        if (newPos == (off_t)-1) throw std::runtime_error("lseek failed");
        mCurrentPosition = (uint64)newPos;
    }

    uint64 getFilePointer() override { return mCurrentPosition; }

    size_t write(const void *, size_t) override { return 0; } // read-only
    void close() override { if (mFd != -1) ::close(mFd); mFd = -1; }

private:
    int mFd;
    uint64 mCurrentPosition;
};

/* Advances elt to its next sibling within ctx. */
static bool
AdvanceSibling(EbmlStream &aStream, const EbmlSemanticContext &ctx,
               EbmlElement *&elt, int &UpperElementLevel, bool bAllowDummy)
{
    if (UpperElementLevel > 0)
    {
        UpperElementLevel--;
        return true;
    }
    delete elt;
    elt = aStream.FindNextElement(ctx, UpperElementLevel, 0xFFFFFFFFL, bAllowDummy);
    return false;
}

/* Deletes elt and advances it to the next sibling within ctx. If stop is true
 * (caller found what it needed, or hit a hard failure on this entry), the
 * function returns true so the caller can break immediately. */
static bool
AdvanceOuterSibling(EbmlStream &aStream, const EbmlSemanticContext &ctx,
                     EbmlElement *&elt, int &UpperElementLevel,
                     bool bAllowDummy, bool stop)
{
    UpperElementLevel = 0;
    elt->SkipData(aStream, ctx);
    delete elt;
    elt = NULL;
    if (stop)
        return true;
    elt = aStream.FindNextElement(ctx, UpperElementLevel, 0xFFFFFFFFL, bAllowDummy);
    return false;
}

extern "C" gboolean
mkv_get_image_region(const gchar *path, guint index,
                     guchar **mmap_base_out, gsize *mmap_base_len_out,
                     guchar **image_data_out, gsize *image_len_out,
                     guint *total_images_out)
{
    int fd;
    struct stat st;
    guchar *base = NULL;
    gsize base_len = 0;

    if (!path || !mmap_base_out || !mmap_base_len_out || !image_data_out || !image_len_out)
        return FALSE;

    if (total_images_out) *total_images_out = 0;

    fd = open(path, O_RDONLY);
    if (fd == -1) return FALSE;

    if (fstat(fd, &st) != 0) { close(fd); return FALSE; }
    base_len = st.st_size;

    base = (guchar *) mmap(0, base_len, PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED)
    {
        close(fd);
        return FALSE;
    }

    try
    {
        FdIOCallback io(fd);
        EbmlStream aStream(io);

        int UpperElementLevel = 0;
        bool bAllowDummy = false;
        bool found = false;
        guint image_count = 0;
        guint64 attachment_offset = 0;
        guint64 attachment_len = 0;

        /* find and skip the EBML head first, matching the reference idiom
         * (test8.cpp / test00.cpp / mkvtree.cpp all do this before ever
         * searching for KaxSegment) rather than relying on FindNextID to
         * scan straight past it unattended */
        EbmlElement *segment_elem = aStream.FindNextID(EBML_INFO(EbmlHead), 0xFFFFFFFFL);
        if (segment_elem)
        {
            segment_elem->SkipData(aStream, EBML_CLASS_CONTEXT(EbmlHead));
            delete segment_elem;
        }

        segment_elem = aStream.FindNextID(EBML_INFO(KaxSegment), 0xFFFFFFFFL);
        if (!segment_elem || EbmlId(*segment_elem) != EBML_ID(KaxSegment))
        {
            delete segment_elem;
            goto fail;
        }

        guint64 segment_data_start = segment_elem->GetElementPosition() + segment_elem->HeadSize();
        bool segment_finite = segment_elem->IsFiniteSize();
        guint64 segment_end = segment_finite ? segment_elem->GetEndPosition() : 0;

        guint64 attachments_pos = 0;
        bool have_seek = false;

        EbmlElement *seekhead_elem = aStream.FindNextElement(EBML_CONTEXT(segment_elem), UpperElementLevel, 0xFFFFFFFFL, bAllowDummy);
        while (seekhead_elem && !have_seek)
        {
            if (UpperElementLevel > 0) break;
            if (UpperElementLevel < 0) UpperElementLevel = 0;

            if (EbmlId(*seekhead_elem) == EBML_ID(KaxAttachments))
            {
                /* no SeekHead entry pointed here, but we walked straight
                 * into it ourselves; use its own position directly instead
                 * of giving up */
                attachments_pos = seekhead_elem->GetElementPosition();
                have_seek = true;
                delete seekhead_elem;
                seekhead_elem = NULL;
                break;
            }

            if (EbmlId(*seekhead_elem) == EBML_ID(KaxSeekHead))
            {
                bool bad_seek = false;
                EbmlElement *seek_elem = aStream.FindNextElement(EBML_CONTEXT(seekhead_elem), UpperElementLevel, 0xFFFFFFFFL, bAllowDummy);
                while (seek_elem)
                {
                    if (UpperElementLevel > 0) break;
                    if (UpperElementLevel < 0) UpperElementLevel = 0;

                    if (EbmlId(*seek_elem) == EBML_ID(KaxSeek))
                    {
                        bool have_id = false, have_pos = false;
                        binary id_buf[4];
                        unsigned int id_len = 0;
                        uint64 seek_pos64 = 0;

                        EbmlElement *seekchild_elem = aStream.FindNextElement(EBML_CONTEXT(seek_elem), UpperElementLevel, 0xFFFFFFFFL, bAllowDummy);
                        while (seekchild_elem)
                        {
                            if (UpperElementLevel > 0) break;
                            if (UpperElementLevel < 0) UpperElementLevel = 0;

                            if (EbmlId(*seekchild_elem) == EBML_ID(KaxSeekID))
                            {
                                KaxSeekID &sid = *static_cast<KaxSeekID *>(seekchild_elem);
                                id_len = (unsigned int)sid.GetSize();
                                if (id_len <= sizeof(id_buf))
                                {
                                    sid.ReadData(aStream.I_O());
                                    memcpy(id_buf, sid.GetBuffer(), id_len);
                                    have_id = true;
                                }
                            }
                            else if (EbmlId(*seekchild_elem) == EBML_ID(KaxSeekPosition))
                            {
                                KaxSeekPosition &spos = *static_cast<KaxSeekPosition *>(seekchild_elem);
                                spos.ReadData(aStream.I_O());
                                if (spos.ValidateSize())
                                {
                                    seek_pos64 = uint64(spos);
                                    have_pos = true;
                                }
                            }
                            else
                            {
                                seekchild_elem->SkipData(aStream, EBML_CONTEXT(seekchild_elem));
                            }

                            if (AdvanceSibling(aStream, EBML_CONTEXT(seek_elem), seekchild_elem,
                                               UpperElementLevel, bAllowDummy))
                                break;

                        }
                        delete seekchild_elem;
                        if (have_id && have_pos)
                        {
                            EbmlId target_id(id_buf, id_len);
                            if (target_id == EBML_ID(KaxAttachments))
                            {
                                if (seek_pos64 > base_len || segment_data_start > base_len - seek_pos64 ||
                                    (segment_elem->IsFiniteSize() && seek_pos64 >= segment_elem->GetSize()))
                                    bad_seek = true;
                                else
                                {
                                    attachments_pos = segment_data_start + seek_pos64;
                                    have_seek = true;
                                }
                            }
                        }
                    }
                    if (AdvanceOuterSibling(aStream, EBML_CONTEXT(seekhead_elem), seek_elem,
                                            UpperElementLevel, bAllowDummy, have_seek || bad_seek))
                        break;
                }

                delete seekhead_elem;
                seekhead_elem = NULL;
                if (have_seek) break;
                /* this SeekHead didn't list an Attachments entry, so keep
                 * scanning the remaining level-1 siblings below instead of
                 * giving up immediately */
                seekhead_elem = aStream.FindNextElement(EBML_CONTEXT(segment_elem), UpperElementLevel, 0xFFFFFFFFL, bAllowDummy);
                continue;
            }

            if (!seekhead_elem->IsFiniteSize())
            {
                /* refuse to SkipData() through an unknown-size element
                 * (e.g. a streamed Cluster); bail out instead of risking
                 * a slow scan */
                delete seekhead_elem;
                seekhead_elem = NULL;
                break;
            }

            seekhead_elem->SkipData(aStream, EBML_CONTEXT(seekhead_elem));
            delete seekhead_elem;
            seekhead_elem = aStream.FindNextElement(EBML_CONTEXT(segment_elem), UpperElementLevel, 0xFFFFFFFFL, bAllowDummy);
        }

        delete segment_elem;

        if (!have_seek)
            goto fail;

        io.setFilePointer(attachments_pos, seek_beginning);
        UpperElementLevel = 0;

        bool bad = false;
        EbmlElement *attachments_elem = aStream.FindNextElement(EBML_CLASS_CONTEXT(KaxSegment), UpperElementLevel, 0xFFFFFFFFL, bAllowDummy);
        if (!attachments_elem || EbmlId(*attachments_elem) != EBML_ID(KaxAttachments))
        {
            bad = true;
        }

        if (!bad)
        {
            guint64 attachments_head = attachments_elem->HeadSize();
            guint64 attachments_start = attachments_elem->GetElementPosition();

            if (attachments_elem->IsFiniteSize())
            {
                guint64 attachments_data_len = attachments_elem->GetSize();

                /* overflow-safe: attachments_start + attachments_head + attachments_data_len <= base_len */
                if (attachments_head > base_len - attachments_start ||
                    attachments_data_len > base_len - attachments_start - attachments_head ||
                /* also stay inside the Segment itself, when the Segment's size is finite */
                    (segment_finite &&
                     attachments_start + attachments_head + attachments_data_len > segment_end))
                {
                    bad = true;
                }
            }
            else
            {
                /* an unknown-size KaxAttachments is itself suspicious --
                 * Attachments is not normally written as a
                 * streamed/unknown-size master */
                bad = true;
            }
        }
        if (bad)
        {
            delete attachments_elem;
            goto fail;
        }

        {
            EbmlElement *attached_elem = aStream.FindNextElement(EBML_CONTEXT(attachments_elem), UpperElementLevel, 0xFFFFFFFFL, bAllowDummy);
            while (attached_elem)
            {
                if (UpperElementLevel > 0) break;
                if (UpperElementLevel < 0) UpperElementLevel = 0;

                if (EbmlId(*attached_elem) == EBML_ID(KaxAttached))
                {
                    std::string mime_type;
                    guint64 data_start = 0;
                    guint64 data_len = 0;
                    bool have_data = false;

                    EbmlElement *attachedchild_elem = aStream.FindNextElement(EBML_CONTEXT(attached_elem), UpperElementLevel, 0xFFFFFFFFL, bAllowDummy);
                    while (attachedchild_elem)
                    {
                        if (UpperElementLevel > 0) break;
                        if (UpperElementLevel < 0) UpperElementLevel = 0;

                        if (EbmlId(*attachedchild_elem) == EBML_ID(KaxMimeType) && attachedchild_elem->GetSize() < 256)
                        {
                            KaxMimeType &mt = *static_cast<KaxMimeType *>(attachedchild_elem);
                            mt.ReadData(aStream.I_O());
                            mime_type = std::string(mt);
                        }
                        else if (EbmlId(*attachedchild_elem) == EBML_ID(KaxFileData))
                        {
                            data_start = attachedchild_elem->GetElementPosition() + attachedchild_elem->HeadSize();
                            data_len = attachedchild_elem->GetSize();
                            have_data = true;
                            attachedchild_elem->SkipData(aStream, EBML_CONTEXT(attachedchild_elem));
                        }
                        else
                        {
                            attachedchild_elem->SkipData(aStream, EBML_CONTEXT(attachedchild_elem));
                        }

                        if (AdvanceSibling(aStream, EBML_CONTEXT(attached_elem), attachedchild_elem,
                                           UpperElementLevel, bAllowDummy))
                            break;
                    }

                    delete attachedchild_elem;
                    if (have_data && mime_type.size() >= 6 && mime_type.compare(0, 6, "image/") == 0)
                    {
                        if (image_count == index)
                        {
                            attachment_offset = data_start;
                            attachment_len = data_len;
                            found = true;
                        }
                        image_count++;
                    }
                }
                if (AdvanceOuterSibling(aStream, EBML_CONTEXT(attachments_elem), attached_elem,
                                        UpperElementLevel, bAllowDummy, false))
                    break;
            }
        }

        delete attachments_elem;

        if (total_images_out) *total_images_out = image_count;

        if (!found ||
            attachment_offset > base_len || attachment_len > base_len - attachment_offset)
        {
            goto fail;
        }

        *mmap_base_out = base;
        *mmap_base_len_out = base_len;
        *image_data_out = base + attachment_offset;
        *image_len_out = attachment_len;
        return TRUE;
    }
    catch (...)
    {
    }
fail:
    munmap(base, base_len);
    return FALSE;
}

#else /* !HAVE_MATROSKA */

extern "C" gboolean
mkv_get_image_region(const gchar *path, guint index
                      guchar **mmap_base_out, gsize *mmap_base_len_out,
                      guchar **image_data_out, gsize *image_len_out,
                      guint *total_images_out)
{
    (void) path;
    (void) index;
    (void) mmap_base_out;
    (void) mmap_base_len_out;
    (void) image_data_out;
    (void) image_len_out;
    if (total_images_out) *total_images_out = 0;
    return FALSE;
}

#endif /* HAVE_MATROSKA */

