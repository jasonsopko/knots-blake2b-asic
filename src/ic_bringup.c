/* ic_bringup.c - the initialization sequence, section 14.
 *
 * Steps 1 to 5 are board level and go through the ic_board hooks; from step 6
 * everything is on the bus. Any failure restarts the whole sequence, up to
 * five attempts, before the chain is declared dead.
 *
 * Step 3 of the datasheet, setting the rail voltage, is absent: the mining
 * controller's voltage setter only stores the value and drives nothing
 * (section 13.4). Whatever programs the ISL8118 is outside this library.
 */
#include "intchains.h"

#define SENSOR_PERIOD_MS 25

/* Register 6, mode 0: set bits 3 and 2, clear bit 0. */
#define SENSOR_MODE_0 0x0Cu

static int gpio(int (*fn)(void *, int), void *ctx, int level)
{
    if (!fn)
        return IC_OK;                 /* board without this signal wired */
    return fn(ctx, level) == 0 ? IC_OK : IC_ERR_IO;
}

static void hold(const struct ic_board *b, unsigned ms)
{
    if (b->delay_ms)
        b->delay_ms(b->ctx, ms);
}

int ic_bringup_once(struct ic_chain *ch, const struct ic_board *b)
{
    uint32_t period;
    int rc, i, cores = 0;

    if (!ch || !ch->fam || !b)
        return IC_ERR_ARG;

    ch->nchips = 0;
    ch->good_cores = 0;

    /* 1. Assert reset, hold 10 ms. */
    rc = gpio(b->set_reset, b->ctx, 0);
    if (rc != IC_OK)
        return rc;
    hold(b, 10);

    /* 2. Assert enable low, hold 20 ms. */
    rc = gpio(b->set_enable, b->ctx, 0);
    if (rc != IC_OK)
        return rc;
    hold(b, 20);

    /* 3. Set target voltage: not driven from the mining path. */

    /* 4. Power the board. */
    rc = gpio(b->set_plugin, b->ctx, 1);
    if (rc != IC_OK)
        return rc;

    /* 5. Release reset: low for 50 ms, then high. */
    rc = gpio(b->set_reset, b->ctx, 0);
    if (rc != IC_OK)
        return rc;
    hold(b, 50);
    rc = gpio(b->set_reset, b->ctx, 1);
    if (rc != IC_OK)
        return rc;

    /* 6. Enumerate. */
    rc = ic_enumerate(ch, NULL);
    if (rc != IC_OK)
        return rc;
    if (ch->nchips < 1)
        return IC_ERR_STATE;

    /* 7. Configure sensors: mode 0, then the 25 ms period in the high half of
     * register 7. The low half holds the reading and is written as zero,
     * which is what a plain 32 bit write of the period field does. */
    rc = ic_write_reg(ch, IC_BROADCAST, IC_REG_SENSOR_MODE, SENSOR_MODE_0);
    if (rc != IC_OK)
        return rc;
    period = (uint32_t)ic_sensor_period(SENSOR_PERIOD_MS) << 16;
    rc = ic_write_reg(ch, IC_BROADCAST, IC_REG_SENSOR_DATA, period);
    if (rc != IC_OK)
        return rc;

    /* 8. Set 50 MHz, broadcast. */
    rc = ic_set_freq(ch, IC_BROADCAST, IC_BRINGUP_MHZ_LOW, NULL);
    if (rc != IC_OK)
        return rc;

    /* 9. Assert enable high, hold 20 ms. */
    rc = gpio(b->set_enable, b->ctx, 1);
    if (rc != IC_OK)
        return rc;
    hold(b, 20);

    /* 10. Self-test. */
    rc = ic_selftest(ch);
    if (rc != IC_OK)
        return rc;

    /* 11. Read good cores from each chip and sum. A read failure aborts. */
    for (i = 1; i <= ch->nchips; i++) {
        uint32_t v;

        rc = ic_read_reg(ch, (uint8_t)i, IC_REG_GOOD_CORES, &v);
        if (rc != IC_OK)
            return rc;
        cores += (int)(v & 0xFF);
    }
    ch->good_cores = cores;

    /* 12. Ramp to 500 MHz. Failure here is logged, not fatal. */
    (void)ic_set_freq(ch, IC_BROADCAST, IC_BRINGUP_MHZ_HIGH, NULL);

    /* 13. Start hashing, then wait 5 s before the first work load. */
    rc = ic_start_hashing(ch, ic_chain_start_value(ch->nchips));
    if (rc != IC_OK)
        return rc;
    hold(b, 5000);

    return IC_OK;
}

int ic_bringup(struct ic_chain *ch, const struct ic_board *b)
{
    int rc = IC_ERR_STATE, i;

    for (i = 0; i < IC_BRINGUP_ATTEMPTS; i++) {
        rc = ic_bringup_once(ch, b);
        if (rc == IC_OK)
            return IC_OK;
    }
    return rc;
}
