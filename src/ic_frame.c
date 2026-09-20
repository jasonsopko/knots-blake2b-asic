/* ic_frame.c - frame assembly and reply search, section 4. */
#include <string.h>

#include "intchains.h"

static void put_pkt(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);      /* packets go out MSB first */
    p[1] = (uint8_t)(v & 0xff);
}

static uint16_t get_pkt(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

int ic_frame_build(uint8_t *out, size_t outsz, uint16_t cmd,
                   const uint16_t *payload, size_t npkt, int with_crc)
{
    size_t len = ic_frame_len(npkt, with_crc);
    size_t i, n = 0;

    if (!out || npkt > IC_MAX_PAYLOAD_PKT || (npkt && !payload) || outsz < len)
        return IC_ERR_ARG;

    put_pkt(out + n, IC_PREAMBLE);  n += 2;
    put_pkt(out + n, cmd);          n += 2;
    for (i = 0; i < npkt; i++, n += 2)
        put_pkt(out + n, payload[i]);

    /* The CRC covers preamble, command and payload. The tail is excluded. */
    if (with_crc) {
        put_pkt(out + n, ic_crc16_wire(out, n));
        n += 2;
    }
    put_pkt(out + n, IC_TAIL);      n += 2;

    return (int)n;
}

int ic_frame_find(const uint8_t *rx, size_t rxlen,
                  const struct ic_tmpl *tmpl, size_t ntmpl,
                  size_t npkt, int has_crc, struct ic_reply *out)
{
    /* preamble + cmd + payload + optional crc; the tail need not be present in
     * the buffer for the reply to be readable. */
    size_t need = (2 + npkt + (has_crc ? 1u : 0u)) * 2u;
    size_t at;
    int saw_match = 0;

    if (!rx || !tmpl || !out || npkt > IC_MAX_PAYLOAD_PKT)
        return IC_ERR_ARG;
    if (rxlen < need)
        return IC_ERR_NOREPLY;

    for (at = 0; at + need <= rxlen; at++) {
        uint16_t cmd;
        size_t t, i;

        if (rx[at] != (uint8_t)(IC_PREAMBLE >> 8) ||
            rx[at + 1] != (uint8_t)(IC_PREAMBLE & 0xff))
            continue;

        cmd = get_pkt(rx + at + 2);
        for (t = 0; t < ntmpl; t++)
            if (ic_tmpl_match(tmpl[t], cmd))
                break;
        if (t == ntmpl)
            continue;               /* a preamble, but not our reply */

        saw_match = 1;
        if (has_crc) {
            size_t covered = (2 + npkt) * 2u;
            uint16_t want = get_pkt(rx + at + covered);
            if (ic_crc16_wire(rx + at, covered) != want)
                continue;           /* keep looking; a later copy may be clean */
        }

        memset(out, 0, sizeof(*out));
        out->offset = at;
        out->cmd    = cmd;
        out->npkt   = npkt;
        out->crc_ok = 1;
        for (i = 0; i < npkt; i++)
            out->pkt[i] = get_pkt(rx + at + 4 + i * 2);
        return IC_OK;
    }

    return saw_match ? IC_ERR_CRC : IC_ERR_NOREPLY;
}

size_t ic_filler_bytes(uint8_t addr, size_t frame_len, size_t reply_len)
{
    size_t filler = (addr == IC_BROADCAST) ? IC_BROADCAST_FILLER
                                           : (size_t)addr * IC_FILLER_PER_HOP;

    /* Six bytes of latency per chip hop, and the chain still has to clock out
     * whatever the reply is longer by. Clocking too few bytes reads as a dead
     * chain, so this is the one rule not to get wrong.
     *
     * The transfer this produces is 6a + max(frame_len, reply_len), which puts
     * the reply in the window starting at the hop latency and ending exactly
     * at the end of the transfer. Do not expect it after the request frame. */
    if (reply_len > frame_len)
        filler += reply_len - frame_len;
    return filler;
}
