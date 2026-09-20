/* ic_ops.c - the operations a controller actually issues, section 7. */
#include <string.h>

#include "intchains.h"

int ic_enumerate(struct ic_chain *ch, int *nchips)
{
    uint16_t payload[1] = { 0x0000 };
    struct ic_reply r;
    int rc;

    /* Broadcast with one zero packet; the reply payload is the chip count.
     * Nothing addressed is meaningful until this has succeeded. */
    rc = ic_xact(ch, IC_CMD(0, IC_OP_AUTOADDR, IC_BROADCAST), payload, 1, &r);
    if (rc != IC_OK)
        return rc;

    ch->nchips = (int)r.pkt[0];
    if (nchips)
        *nchips = ch->nchips;
    return IC_OK;
}

int ic_selftest(struct ic_chain *ch)
{
    static const uint8_t phases[3] = {
        IC_OP_SELFTEST_1, IC_OP_SELFTEST_2, IC_OP_SELFTEST_3
    };
    int i, rc;

    /* Always the broadcast triple 01 -> 0B -> 03, no payload, no retries. The
     * controller does not distinguish which phase failed. */
    for (i = 0; i < 3; i++) {
        rc = ic_xact(ch, IC_CMD(0, phases[i], IC_BROADCAST), NULL, 0, NULL);
        if (rc != IC_OK)
            return rc;
    }
    return IC_OK;
}

int ic_read_reg(struct ic_chain *ch, uint8_t addr, uint16_t reg, uint32_t *val)
{
    uint16_t payload[1];
    struct ic_reply r;
    int rc;

    if (!val)
        return IC_ERR_ARG;
    payload[0] = reg;
    rc = ic_xact(ch, IC_CMD(0, IC_OP_READREG, addr), payload, 1, &r);
    if (rc != IC_OK)
        return rc;

    /* Packet 0 is a status word the controller ignores; the value comes back
     * high word first in packets 1 and 2. */
    *val = (uint32_t)r.pkt[1] << 16 | r.pkt[2];
    return IC_OK;
}

int ic_write_reg(struct ic_chain *ch, uint8_t addr, uint16_t reg, uint32_t val)
{
    uint16_t payload[3];

    payload[0] = reg;
    payload[1] = (uint16_t)(val >> 16);
    payload[2] = (uint16_t)(val & 0xFFFF);
    return ic_xact(ch, IC_CMD(0, IC_OP_WRITEREG, addr), payload, 3, NULL);
}

uint32_t ic_chain_start_value(int nchips)
{
    /* The index-budget hypothesis of section 13.1: min(8, 14 - ceil(log2 n)).
     * It reproduces every observed product, and it is still a hypothesis. */
    int bits = 0, n;

    if (nchips < 1)
        return IC_CHAIN_START_NIB_8 | IC_CHAIN_START_LOW;
    for (n = 1; n < nchips; n <<= 1)
        bits++;
    if (14 - bits > 8)
        bits = 14 - 8;
    if (14 - bits < 0)
        bits = 14;
    return (uint32_t)(14 - bits) << 28 | IC_CHAIN_START_LOW;
}

int ic_start_hashing(struct ic_chain *ch, uint32_t reg4)
{
    /* The final bring-up step. This is what starts the chain hashing; the
     * controller writes it and never reads it back. */
    return ic_write_reg(ch, IC_BROADCAST, IC_REG_CHAIN_START, reg4);
}

int ic_poll(struct ic_chain *ch, struct ic_result *res)
{
    struct ic_reply r;
    int rc;

    rc = ic_xact(ch, IC_CMD(0, IC_OP_POLL, IC_BROADCAST), NULL, 0, &r);
    if (rc != IC_OK)
        return rc;

    /* An echo of 0800 means the chain has nothing. Anything else matching the
     * opcode is a result; there is no other notification path. */
    if (r.npkt == 0)
        return 0;

    if (res)
        ic_result_decode(r.cmd, r.pkt, res);
    return 1;
}

int ic_load_work(struct ic_chain *ch, uint8_t work_id,
                 const uint8_t *payload, size_t len)
{
    uint16_t cfg[1] = { IC_WORKCFG_OPERAND };
    uint16_t pkt[IC_MAX_PAYLOAD_PKT];
    size_t i, npkt;
    int rc;

    if (!ch || !ch->fam || !payload)
        return IC_ERR_ARG;
    if (len != ch->fam->work_bytes)
        return IC_ERR_ARG;

    npkt = len / 2;
    for (i = 0; i < npkt; i++)
        pkt[i] = (uint16_t)((uint16_t)payload[i * 2] << 8 | payload[i * 2 + 1]);

    /* Work configuration is broadcast immediately before every work load, with
     * the same constant every time (section 7.3). */
    rc = ic_xact(ch, IC_CMD(0, IC_OP_WORKCFG, IC_BROADCAST), cfg, 1, NULL);
    if (rc != IC_OK)
        return rc;

    /* The work ID rides in the tag nibble and comes back on every result for
     * this work item. Work is always broadcast: every chip gets the same
     * payload and the same starting nonce. */
    return ic_xact(ch, IC_CMD(work_id, IC_OP_LOADWORK, IC_BROADCAST),
                   pkt, npkt, NULL);
}
