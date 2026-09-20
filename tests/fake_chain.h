#ifndef FAKE_CHAIN_H
#define FAKE_CHAIN_H

#include "intchains.h"

#define FAKE_NREG 16

struct fake_chain {
    const struct ic_family *fam;
    int      nchips;
    int      good_cores_per_chip;
    uint32_t reg[FAKE_NREG];
    uint16_t sensor_period;
    uint16_t sensor_raw;

    int      have_result;
    struct ic_result result;

    uint16_t last_cmd;
    uint16_t last_workcfg;
    uint8_t  last_work_id;
    uint8_t  last_work[IC_MAX_PAYLOAD_PKT * 2];
    size_t   last_work_len;

    int      fail_transport;
    int      xfers;
};

void     fake_chain_init(struct fake_chain *fc, const struct ic_family *fam, int nchips);
struct ic_transport fake_chain_transport(struct fake_chain *fc);
int      fake_chain_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
uint32_t fake_chain_reg(struct fake_chain *fc, uint8_t addr, uint16_t reg);
void     fake_chain_set_reg(struct fake_chain *fc, uint8_t addr, uint16_t reg, uint32_t val);

#endif
