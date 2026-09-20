#include "intchains.h"
#include "ic_test.h"

static void decodes_a_result(void)
{
    uint16_t pkt[5] = { 0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x2A07 };
    struct ic_result r;

    /* Y8NN: work ID 3, opcode 8, chip 5. */
    ic_result_decode(0x3805, pkt, &r);
    CHECK_EQ(r.work_id, 3);
    CHECK_EQ(r.chip, 5);
    CHECK(r.nonce == UINT64_C(0xDEF09ABC56781234));
    CHECK_EQ(r.ts_index, 0x07);
    CHECK_EQ(r.core, 0x2A);
}

static void nonce_goes_back_into_the_header(void)
{
    uint8_t b[8];

    /* Low word little-endian, then the high word little-endian. */
    ic_result_nonce_bytes(UINT64_C(0xDEF09ABC56781234), b);
    CHECK_EQ(b[0], 0x34);
    CHECK_EQ(b[1], 0x12);
    CHECK_EQ(b[2], 0x78);
    CHECK_EQ(b[3], 0x56);
    CHECK_EQ(b[4], 0xBC);
    CHECK_EQ(b[5], 0x9A);
    CHECK_EQ(b[6], 0xF0);
    CHECK_EQ(b[7], 0xDE);
}

static void nonce_stays_under_the_ceiling(void)
{
    /* Nothing enforces this, but a result above the ceiling would mean the
     * chip ignored the bound we sent. */
    CHECK(ic_ica586.nonce_ceiling < ic_ict580.nonce_ceiling);
    CHECK_EQ(ic_ict580.nonce_ceiling, ic_icc590.nonce_ceiling);
}

static void register_four_hypothesis(void)
{
    /* Table 13-1, every product. The top nibble is what the index-budget
     * hypothesis predicts; the low half is identical everywhere. */
    CHECK_EQ(ic_chain_start_value(96), 0x700003F1u);   /* HS6 */
    CHECK_EQ(ic_chain_start_value(84), 0x700003F1u);   /* HS6-SE, SC6-SE, SC5 Pro II */
    CHECK_EQ(ic_chain_start_value(46), 0x800003F1u);   /* HS3, HS5, HS-LITE */
    CHECK_EQ(ic_chain_start_value(36), 0x800003F1u);   /* SCBox II, HS-BOX II */
    CHECK_EQ(ic_chain_start_value(16), 0x800003F1u);   /* SCBox, HS-BOX, HS3-SE */

    /* The prediction the family cannot test: 128 chips still reads 7, and the
     * first 6 appears at 129. */
    CHECK_EQ(ic_chain_start_value(128), 0x700003F1u);
    CHECK_EQ(ic_chain_start_value(129), 0x600003F1u);
}

TEST_MAIN("result",
    decodes_a_result();
    nonce_goes_back_into_the_header();
    nonce_stays_under_the_ceiling();
    register_four_hypothesis();
)
