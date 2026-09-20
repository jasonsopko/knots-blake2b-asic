#include "intchains.h"
#include "ic_test.h"
#include "fake_chain.h"

static struct fake_chain fc;
static struct ic_chain ch;

static void setup(const struct ic_family *fam, int nchips)
{
    fake_chain_init(&fc, fam, nchips);
    ic_chain_init(&ch, fam, fake_chain_transport(&fc));
}

static void enumerate_reports_the_chip_count(void)
{
    int n = 0;

    setup(&ic_ica586, 84);                 /* SC5 Pro II */
    CHECK_EQ(ic_enumerate(&ch, &n), IC_OK);
    CHECK_EQ(n, 84);
    CHECK_EQ(ch.nchips, 84);
    CHECK_EQ(fc.last_cmd, 0x0200);
}

static void selftest_is_the_broadcast_triple(void)
{
    setup(&ic_ict580, 16);
    CHECK_EQ(ic_selftest(&ch), IC_OK);
    CHECK_EQ(fc.last_cmd, 0x0300);         /* 01 then 0B then 03 */
    CHECK_EQ(fc.xfers, 3);
}

static void register_round_trip(void)
{
    uint32_t v = 0;

    setup(&ic_icc590, 46);
    CHECK_EQ(ic_write_reg(&ch, 1, IC_REG_PLL, 0x00600024u), IC_OK);
    CHECK_EQ(ic_read_reg(&ch, 1, IC_REG_PLL, &v), IC_OK);
    CHECK_EQ(v, 0x00600024u);

    /* Good cores comes back per chip and is summed by bring-up. */
    CHECK_EQ(ic_read_reg(&ch, 3, IC_REG_GOOD_CORES, &v), IC_OK);
    CHECK_EQ(v, 20);
}

static void a_reply_from_the_far_end_still_arrives(void)
{
    uint32_t v = 0;

    /* Chip 84 is 504 filler bytes away. If the turnaround were computed from
     * the end of the request frame the reply would land past the buffer. */
    setup(&ic_ica586, 84);
    CHECK_EQ(ic_read_reg(&ch, 84, IC_REG_GOOD_CORES, &v), IC_OK);
    CHECK_EQ(v, 20);
}

static void poll_distinguishes_empty_from_a_result(void)
{
    struct ic_result r;

    setup(&ic_ict580, 36);
    CHECK_EQ(ic_poll(&ch, &r), 0);         /* an echo of 0800 */

    fc.have_result = 1;
    fc.result.work_id  = 5;
    fc.result.chip     = 12;
    fc.result.core     = 63;
    fc.result.ts_index = 2;
    fc.result.nonce    = UINT64_C(0x0000123456789ABC);

    memset(&r, 0, sizeof(r));
    CHECK_EQ(ic_poll(&ch, &r), 1);
    CHECK_EQ(r.work_id, 5);
    CHECK_EQ(r.chip, 12);
    CHECK_EQ(r.core, 63);
    CHECK_EQ(r.ts_index, 2);
    CHECK(r.nonce == UINT64_C(0x0000123456789ABC));

    CHECK_EQ(ic_poll(&ch, &r), 0);         /* drained */
}

static void load_work_sends_the_configuration_word_first(void)
{
    struct ic_work w;
    uint8_t header[80], payload[160];
    int len;

    setup(&ic_ict580, 16);
    memset(header, 0x5A, sizeof(header));
    memset(&w, 0, sizeof(w));
    w.header = header;
    w.header_len = sizeof(header);
    w.nonce_start = 0;
    w.timestamp = 0x68A0B0C0;
    memset(w.target, 0xFF, sizeof(w.target));

    len = ic_work_build(&ic_ict580, &w, payload, sizeof(payload));
    CHECK_EQ(len, 96);

    CHECK_EQ(ic_load_work(&ch, 9, payload, (size_t)len), IC_OK);
    CHECK_EQ(fc.last_workcfg, IC_WORKCFG_OPERAND);
    CHECK_EQ(fc.last_work_id, 9);
    CHECK_EQ(fc.last_work_len, 96);
    CHECK(memcmp(fc.last_work, payload, 96) == 0);

    /* The work ID rides in the tag nibble of a broadcast command. */
    CHECK_EQ(fc.last_cmd, 0x9700);
}

static void the_wrong_payload_length_is_refused(void)
{
    uint8_t payload[160];

    setup(&ic_ict580, 16);
    memset(payload, 0, sizeof(payload));
    CHECK_EQ(ic_load_work(&ch, 0, payload, 144), IC_ERR_ARG);
}

static void a_dead_transport_exhausts_the_retries(void)
{
    int n = 0;

    setup(&ic_icc590, 96);
    fc.fail_transport = 1;
    CHECK_EQ(ic_enumerate(&ch, &n), IC_ERR_IO);
    CHECK_EQ(fc.xfers, 2);                 /* one attempt plus one retry */
}

static int nop_gpio(void *ctx, int level) { (void)ctx; (void)level; return 0; }
static void nop_delay(void *ctx, unsigned ms) { (void)ctx; (void)ms; }

static void bringup_runs_the_whole_sequence(void)
{
    struct ic_board board;

    setup(&ic_ica586, 84);
    memset(&board, 0, sizeof(board));
    board.set_reset  = nop_gpio;
    board.set_enable = nop_gpio;
    board.set_plugin = nop_gpio;
    board.delay_ms   = nop_delay;

    CHECK_EQ(ic_bringup(&ch, &board), IC_OK);
    CHECK_EQ(ch.nchips, 84);
    CHECK_EQ(ch.good_cores, 84 * 20);

    /* Sensors configured, and the chain started with the register 4 value. */
    CHECK_EQ(fc.reg[IC_REG_SENSOR_MODE], 0x0Cu);
    CHECK_EQ(fc.sensor_period, 555);
    CHECK_EQ(fc.reg[IC_REG_CHAIN_START], 0x700003F1u);

    /* Register 0 ends with bypass released. */
    CHECK_EQ(fc.reg[IC_REG_PLL] & 0x80u, 0);
    CHECK_EQ((fc.reg[IC_REG_PLL] >> 16) & 0x1FF, 40);   /* 500 MHz */
}

TEST_MAIN("cmd",
    enumerate_reports_the_chip_count();
    selftest_is_the_broadcast_triple();
    register_round_trip();
    a_reply_from_the_far_end_still_arrives();
    poll_distinguishes_empty_from_a_result();
    load_work_sends_the_configuration_word_first();
    the_wrong_payload_length_is_refused();
    a_dead_transport_exhausts_the_retries();
    bringup_runs_the_whole_sequence();
)
