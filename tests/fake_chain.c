/* fake_chain.c - a chain of Intchains parts, in software.
 *
 * Enough of the device to exercise the transaction layer without hardware: it
 * answers the commands the controller issues, places its reply somewhere in
 * the receive buffer rather than at a fixed offset, and lets a test hand back
 * a result. It is built from the datasheet, so it proves the library agrees
 * with our reading of the datasheet, not with silicon.
 */
#include <string.h>

#include "fake_chain.h"

static void put_pkt(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static uint16_t get_pkt(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

int fake_chain_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    struct fake_chain *fc = ctx;
    uint8_t reply[IC_MAX_FRAME_BYTES];
    uint16_t cmd, payload[IC_MAX_PAYLOAD_PKT];
    const struct ic_cmd_desc *d;
    size_t frame_len, npkt, at, i;
    int rlen = 0;

    memset(rx, 0, len);
    fc->xfers++;

    if (len < 8 || get_pkt(tx) != IC_PREAMBLE)
        return fc->fail_transport ? -1 : 0;

    cmd = get_pkt(tx + 2);
    d = ic_cmd_desc(IC_CMD_OP(cmd));
    if (!d)
        return 0;

    npkt = (size_t)(d->tx_npkt < 0 ? (int)fc->fam->work_pkt : d->tx_npkt);
    for (i = 0; i < npkt; i++)
        payload[i] = get_pkt(tx + 4 + i * 2);
    frame_len = ic_frame_len(npkt, d->tx_crc);

    fc->last_cmd = cmd;

    if (fc->fail_transport)
        return -1;

    switch (IC_CMD_OP(cmd)) {
    case IC_OP_AUTOADDR:
        rlen = ic_frame_build(reply, sizeof(reply), cmd,
                              (uint16_t[]){ (uint16_t)fc->nchips }, 1, 0);
        break;

    case IC_OP_SELFTEST_1:
    case IC_OP_SELFTEST_2:
    case IC_OP_SELFTEST_3:
        rlen = ic_frame_build(reply, sizeof(reply), cmd, NULL, 0, 0);
        break;

    case IC_OP_WORKCFG:
        fc->last_workcfg = payload[0];
        rlen = ic_frame_build(reply, sizeof(reply), cmd, payload, 1, 0);
        break;

    case IC_OP_READREG: {
        uint32_t v = fake_chain_reg(fc, IC_CMD_ADDR(cmd), payload[0]);
        uint16_t out[3];

        out[0] = 0x0000;                  /* status word, ignored by the host */
        out[1] = (uint16_t)(v >> 16);
        out[2] = (uint16_t)(v & 0xFFFF);
        rlen = ic_frame_build(reply, sizeof(reply),
                              IC_CMD(1, IC_OP_READREG, IC_CMD_ADDR(cmd)),
                              out, 3, 1);
        break;
    }

    case IC_OP_WRITEREG: {
        uint32_t v = (uint32_t)payload[1] << 16 | payload[2];

        fake_chain_set_reg(fc, IC_CMD_ADDR(cmd), payload[0], v);
        rlen = ic_frame_build(reply, sizeof(reply), cmd,
                              (uint16_t[]){ 0, 0, 0 }, 3, 0);
        break;
    }

    case IC_OP_LOADWORK:
        fc->last_work_id = IC_CMD_TAG(cmd);
        fc->last_work_len = npkt * 2;
        for (i = 0; i < npkt; i++)
            put_pkt(fc->last_work + i * 2, payload[i]);
        return 0;                          /* the reply is all zeros */

    case IC_OP_POLL:
        if (!fc->have_result) {
            rlen = ic_frame_build(reply, sizeof(reply), cmd, NULL, 0, 0);
        } else {
            uint16_t out[5];
            uint64_t n = fc->result.nonce;

            out[0] = (uint16_t)(n & 0xFFFF);
            out[1] = (uint16_t)((n >> 16) & 0xFFFF);
            out[2] = (uint16_t)((n >> 32) & 0xFFFF);
            out[3] = (uint16_t)((n >> 48) & 0xFFFF);
            out[4] = (uint16_t)((uint16_t)fc->result.core << 8 | fc->result.ts_index);
            rlen = ic_frame_build(reply, sizeof(reply),
                                  IC_CMD(fc->result.work_id, IC_OP_POLL,
                                         fc->result.chip),
                                  out, 5, 1);
            fc->have_result = 0;
        }
        break;

    default:
        return 0;
    }

    if (rlen <= 0)
        return 0;

    /* The reply starts at the hop latency, measured from the beginning of the
     * transfer rather than from the end of the request. That follows from the
     * turnaround rule: frame + 6a + max(0, reply - frame) is 6a + max(frame,
     * reply), so a reply placed at 6a ends exactly at the end of the transfer.
     * Placing it after the request frame instead would run off the end. */
    (void)frame_len;
    at = (size_t)(IC_CMD_ADDR(cmd) == IC_BROADCAST
                  ? IC_FILLER_PER_HOP * fc->nchips
                  : IC_FILLER_PER_HOP * IC_CMD_ADDR(cmd));
    if (at + (size_t)rlen > len)
        return 0;                          /* host clocked too few bytes */
    memcpy(rx + at, reply, (size_t)rlen);
    return 0;
}

uint32_t fake_chain_reg(struct fake_chain *fc, uint8_t addr, uint16_t reg)
{
    if (reg == IC_REG_GOOD_CORES)
        return (uint32_t)fc->good_cores_per_chip;
    if (reg == IC_REG_SENSOR_DATA)
        return (uint32_t)fc->sensor_period << 16 | fc->sensor_raw;
    if (reg < FAKE_NREG)
        return fc->reg[reg];
    (void)addr;
    return 0;
}

void fake_chain_set_reg(struct fake_chain *fc, uint8_t addr, uint16_t reg,
                        uint32_t val)
{
    (void)addr;
    if (reg == IC_REG_SENSOR_DATA)
        fc->sensor_period = (uint16_t)(val >> 16);
    if (reg < FAKE_NREG)
        fc->reg[reg] = val;
}

void fake_chain_init(struct fake_chain *fc, const struct ic_family *fam,
                     int nchips)
{
    memset(fc, 0, sizeof(*fc));
    fc->fam = fam;
    fc->nchips = nchips;
    fc->good_cores_per_chip = fam->cores_per_chip;
    fc->sensor_raw = 2900;                 /* a plausible warm reading */
}

struct ic_transport fake_chain_transport(struct fake_chain *fc)
{
    struct ic_transport tr;

    tr.xfer = fake_chain_xfer;
    tr.ctx  = fc;
    return tr;
}
