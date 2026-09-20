/* ic_work.c - work payload assembly, section 9.
 *
 * All three bound parts share the first 0x50 bytes and the same 16 byte
 * trailer: a 64 bit target bound followed by a 64 bit nonce ceiling. They
 * differ in what sits between them.
 *
 *   ICT580  96 B   header 80          trailer at 0x50
 *   ICC590  144 B  header 128         trailer at 0x80
 *   ICA586  112 B  header 80, then 16 reserved bytes, trailer at 0x60
 */
#include <string.h>

#include "intchains.h"

#define OFF_NONCE     0x20
#define OFF_TIMESTAMP 0x28

/* ASSUMPTION. The datasheet fixes the byte order of the target words and of
 * the nonce as it comes back in a result, but not of the nonce start, the
 * timestamp or the ceiling. These three are written little-endian, which is
 * what a plain 64 bit store on the ARM controller produces and what the result
 * path implies for the nonce. Listed in docs/open-questions.md. */
static void put_u64le(uint8_t *p, uint64_t v)
{
    int i;

    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

/* A 32 bit word loaded from the target, byte-swapped, and stored back is a
 * plain byte reversal, whatever the host's own endianness. */
static void put_rev32(uint8_t *dst, const uint8_t *src)
{
    dst[0] = src[3];
    dst[1] = src[2];
    dst[2] = src[1];
    dst[3] = src[0];
}

int ic_work_build(const struct ic_family *fam, const struct ic_work *w,
                  uint8_t *out, size_t outsz)
{
    size_t trailer;

    if (!fam || !w || !out || !w->header)
        return IC_ERR_ARG;
    if (outsz < fam->work_bytes)
        return IC_ERR_ARG;
    if (w->header_len != fam->header_bytes)
        return IC_ERR_ARG;
    /* ICC551 is compiled into the controller but bound by no product, so its
     * nonce ceiling exists nowhere to be read. Refuse rather than guess. */
    if (fam->nonce_ceiling == 0)
        return IC_ERR_ARG;

    memset(out, 0, fam->work_bytes);

    /* The header is copied verbatim with no byte swapping; the chip runs the
     * whole Blake2B compression and is given no midstate. */
    memcpy(out, w->header, w->header_len);

    put_u64le(out + OFF_NONCE, w->nonce_start);
    put_u64le(out + OFF_TIMESTAMP, w->timestamp);

    trailer = fam->work_bytes - 16;

    /* Top 64 bits of the 256 bit target: high word from target bytes [28:32],
     * low word from [24:28], each byte-swapped. */
    put_rev32(out + trailer, w->target + 28);
    put_rev32(out + trailer + 4, w->target + 24);

    put_u64le(out + trailer + 8, fam->nonce_ceiling);

    /* ICA586 stores 0x13 and 0xDF over the top two bytes of the target bound
     * after writing it. What that does is unknown (section 9, speculative
     * note); it is what the controller emits, nothing more. Keyed off the
     * quirk rather than the struct's address, so a caller holding their own
     * copy of the family still gets the right payload. */
    if (fam->quirks & IC_QUIRK_TARGET_TAG_13DF) {
        out[trailer] = 0x13;
        out[trailer + 1] = 0xDF;
    }

    return (int)fam->work_bytes;
}
