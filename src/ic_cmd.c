/* ic_cmd.c - the command table and the transaction layer, sections 6 and 7.
 *
 * One command is one full-duplex SPI transfer: the request frame, then enough
 * filler to clock the reply back out of the chain, then a search of the
 * receive buffer for a frame whose command word matches a template.
 */
#include <string.h>

#include "intchains.h"

#define T(v, m)    { (uint16_t)(v), (uint16_t)(m) }
#define ECHO(op)   T(((op) << 8), 0xFF00)      /* 0oNN, address don't-care */

static const struct ic_cmd_desc cmds[] = {
    /* op, name, tx_npkt, tx_crc, retries, reply_zeros, replies, nreply */
    { IC_OP_SELFTEST_1, "selftest1", 0,  0,  0, 0, { { ECHO(0x1), 0, 0 } }, 1 },
    { IC_OP_AUTOADDR,   "autoaddr",  1,  0,  1, 0, { { T(0x0200, 0xFFFF), 1, 0 } }, 1 },
    { IC_OP_SELFTEST_3, "selftest3", 0,  0,  0, 0, { { ECHO(0x3), 0, 0 } }, 1 },
    { IC_OP_WORKCFG,    "workcfg",   1,  0,  1, 0, { { ECHO(0x4), 1, 0 } }, 1 },
    { IC_OP_LOADWORK,   "loadwork", -1,  1,  5, 1, { { T(0x0000, 0x0F00), -1, 0 } }, 1 },
    /* A poll answers with either an echo of 0800 (the chain has nothing) or a
     * result tagged Y8NN. The result form is tried first because the echo
     * template would also match a result frame's command word. */
    { IC_OP_POLL,       "poll",      0,  0, 10, 0, { { T(0x0800, 0x0F00), 5, 1 },
                                                     { T(0x0800, 0xFFFF), 0, 0 } }, 2 },
    { IC_OP_WRITEREG,   "writereg",  3,  1,  5, 0, { { ECHO(0x9), 3, 0 } }, 1 },
    { IC_OP_READREG,    "readreg",   1,  0,  5, 0, { { T(0x1A00, 0xFF00), 3, 1 } }, 1 },
    { IC_OP_SELFTEST_2, "selftest2", 0,  0,  0, 0, { { ECHO(0xB), 0, 0 } }, 1 },
    /* 0C00 is defined in all four command tables and issued by nothing. The
     * shape is from the table; the meaning is unknown (section 13.3). */
    { IC_OP_VENDOR_C,   "vendor0c",  5,  0,  0, 0, { { T(0x0C00, 0xFFFF), 15, 1 } }, 1 }
};

const struct ic_cmd_desc *ic_cmd_desc(uint8_t op)
{
    size_t i;

    for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        if (cmds[i].op == op)
            return &cmds[i];
    return NULL;
}

static int resolve_npkt(int npkt, const struct ic_family *fam)
{
    return npkt < 0 ? (int)fam->work_pkt : npkt;
}

static int all_zero(const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        if (p[i])
            return 0;
    return 1;
}

int ic_xact(struct ic_chain *ch, uint16_t cmd,
            const uint16_t *payload, size_t npkt, struct ic_reply *reply)
{
    const struct ic_cmd_desc *d;
    struct ic_reply local;
    uint8_t addr;
    size_t frame_len, reply_max = 0, total;
    int want_npkt, attempt, rc, frame, i;

    if (!ch || !ch->fam)
        return IC_ERR_ARG;
    d = ic_cmd_desc(IC_CMD_OP(cmd));
    if (!d)
        return IC_ERR_ARG;

    addr = IC_CMD_ADDR(cmd);
    want_npkt = resolve_npkt(d->tx_npkt, ch->fam);
    if ((int)npkt != want_npkt)
        return IC_ERR_ARG;

    frame = ic_frame_build(ch->tx, sizeof(ch->tx), cmd, payload, npkt, d->tx_crc);
    if (frame < 0)
        return frame;
    frame_len = (size_t)frame;

    for (i = 0; i < d->nreply; i++) {
        size_t len = ic_frame_len((size_t)resolve_npkt(d->reply[i].npkt, ch->fam),
                                  d->reply[i].crc);
        if (len > reply_max)
            reply_max = len;
    }
    if (d->reply_zeros && reply_max < frame_len)
        reply_max = frame_len;

    total = frame_len + ic_filler_bytes(addr, frame_len, reply_max);
    if (total > sizeof(ch->tx))
        return IC_ERR_ARG;
    memset(ch->tx + frame_len, 0, total - frame_len);

    if (!reply)
        reply = &local;

    /* Any reply that matches no template is discarded and the transaction
     * retried (section 4.1). The retry budget is per command, from table 7-1. */
    rc = IC_ERR_NOREPLY;
    for (attempt = 0; attempt <= d->retries; attempt++) {
        memset(ch->rx, 0, total);
        if (ch->tr.xfer(ch->tr.ctx, ch->tx, ch->rx, total) != 0) {
            rc = IC_ERR_IO;
            continue;
        }

        if (d->reply_zeros) {
            /* Load work expects an all-zero frame of the same length. There
             * is no preamble to search for, so all this can do is confirm the
             * line was quiet: an idle MISO and a chain that answered with
             * zeros look identical. It catches garbage, nothing more. */
            memset(reply, 0, sizeof(*reply));
            if (all_zero(ch->rx + frame_len, reply_max))
                return IC_OK;
            rc = IC_ERR_NOREPLY;
            continue;
        }

        for (i = 0; i < d->nreply; i++) {
            int n = resolve_npkt(d->reply[i].npkt, ch->fam);
            rc = ic_frame_find(ch->rx, total, &d->reply[i].tmpl, 1,
                               (size_t)n, d->reply[i].crc, reply);
            if (rc == IC_OK)
                return IC_OK;
        }
    }

    return rc;
}
