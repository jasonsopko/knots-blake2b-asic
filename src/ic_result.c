/* ic_result.c - result decoding, section 10. */
#include "intchains.h"

void ic_result_decode(uint16_t cmd, const uint16_t *pkt, struct ic_result *out)
{
    uint32_t lo, hi;

    if (!pkt || !out)
        return;

    out->work_id = IC_CMD_TAG(cmd);    /* the work this result belongs to */
    out->chip    = IC_CMD_ADDR(cmd);

    lo = (uint32_t)pkt[1] << 16 | pkt[0];
    hi = (uint32_t)pkt[3] << 16 | pkt[2];
    out->nonce = (uint64_t)hi << 32 | lo;

    out->ts_index = (uint8_t)(pkt[4] & 0xFF);
    out->core     = (uint8_t)(pkt[4] >> 8);
}

void ic_result_nonce_bytes(uint64_t nonce, uint8_t out[8])
{
    uint32_t lo = (uint32_t)(nonce & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(nonce >> 32);
    int i;

    /* The low word little-endian followed by the high word little-endian,
     * which is where they go at header offset 32. */
    for (i = 0; i < 4; i++)
        out[i] = (uint8_t)(lo >> (8 * i));
    for (i = 0; i < 4; i++)
        out[4 + i] = (uint8_t)(hi >> (8 * i));
}
