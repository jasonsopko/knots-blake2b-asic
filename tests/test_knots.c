#include "intchains_knots.h"
#include "ic_crypto.h"
#include "ic_test.h"
#include "knots_vectors.h"

static void show(const char *what, const uint8_t *got, const uint8_t *want, size_t n)
{
    size_t i;

    if (memcmp(got, want, n) == 0)
        return;
    printf("  %s\n    got  ", what);
    for (i = 0; i < n; i++) printf("%02x", got[i]);
    printf("\n    want ");
    for (i = 0; i < n; i++) printf("%02x", want[i]);
    printf("\n");
    ic_test_fails++;
}

/* Every stage of CBlockHeader::GetHash(), against the Knots test data. */
static void every_stage_matches_the_node(void)
{
    size_t i;

    for (i = 0; i < KNOTS_NVECTORS; i++) {
        const struct knots_vector *v = &knots_vectors[i];
        uint8_t h1[32], h2[32], hash1[32], mask[32], pow[32];
        uint8_t payload[160];
        int len;

        printf("  %s\n", v->name);

        ic_knots_h1(&v->h, h1);
        show("h1", h1, v->h1, 32);

        ic_knots_h2(h1, v->h.mm_rhs, h2);
        show("h2", h2, v->h2, 32);

        ic_knots_hash1(h2, v->h.extranonce, hash1);
        show("blake2b stage 1", hash1, v->blake2b_1, 32);

        CHECK_EQ(ic_knots_profile(&v->h), v->profile);
        len = ic_knots_payload(&v->h, payload, sizeof(payload));
        CHECK_EQ(len, (int)v->asic_len);
        CHECK_EQ(len, (int)ic_knots_payload_len(v->profile));
        if (len > 0)
            show("asic input", payload, v->asic_input, (size_t)len);

        ic_knots_xor_mask(v->h.xor_key, v->h.xor_key_clear_bits, mask);
        show("xor mask", mask, v->mask, 32);

        ic_knots_pow_hash(payload, (size_t)len, mask, pow);
        show("block hash", pow, v->block_hash, 32);
    }
}

/* The chip changes the four fields in place; we have to read them back. */
static void rolled_fields_round_trip(void)
{
    size_t i;

    for (i = 0; i < KNOTS_NVECTORS; i++) {
        const struct knots_vector *v = &knots_vectors[i];
        uint8_t payload[160];
        uint32_t n, n2, to, n3;

        CHECK(ic_knots_payload(&v->h, payload, sizeof(payload)) > 0);
        ic_knots_payload_get(payload, v->profile, &n, &n2, &to, &n3);
        CHECK_EQ(n, v->h.nonce);
        CHECK_EQ(n2, v->h.nonce2);
        CHECK_EQ(to, v->h.time_offset);
        CHECK_EQ(n3, v->h.nonce3);
    }
}

static void a_result_lands_in_the_right_fields(void)
{
    const struct knots_vector *v = &knots_vectors[0];
    uint8_t payload[160];
    uint32_t n, n2, to, n3;

    CHECK(ic_knots_payload(&v->h, payload, sizeof(payload)) > 0);

    /* The chip reports an 8 byte nonce and an 8 byte timestamp. On profile 0
     * those cover nNonce + m_nonce2 and m_time_offset + m_nonce3. */
    ic_knots_apply_result(payload, 0, UINT64_C(0x0000DDCCBBAA9988),
                          UINT64_C(0x0000112233445566));
    ic_knots_payload_get(payload, 0, &n, &n2, &to, &n3);
    CHECK_EQ(n, 0xBBAA9988u);
    CHECK_EQ(n2, 0x0000DDCCu);
    CHECK_EQ(to, 0x33445566u);
    CHECK_EQ(n3, 0x00001122u);

    /* And the hash of the edited buffer is still a well formed PoW value. */
    {
        uint8_t pow[32], mask[32];

        ic_knots_xor_mask(v->h.xor_key, v->h.xor_key_clear_bits, mask);
        ic_knots_pow_hash(payload, 80, mask, pow);
        CHECK(memcmp(pow, v->block_hash, 32) != 0);   /* we changed the nonce */
    }
}

/* Which parts can hash which profile, which is the whole point. */
static void profile_zero_is_the_sia_layout(void)
{
    CHECK_EQ(ic_knots_payload_len(0), 80);
    CHECK_EQ(ic_knots_roll_offset(0), IC_CHIP_ROLL_OFFSET);

    /* The Siacoin parts take it as-is. */
    CHECK(ic_knots_fits(&ic_ict580, 0));
    CHECK(ic_knots_fits(&ic_ica586, 0));

    /* The Handshake part is the wrong length for profile 0, and profile 2 is
     * its length but rolls 0x50 instead of 0x20. */
    CHECK(!ic_knots_fits(&ic_icc590, 0));
    CHECK_EQ(ic_knots_payload_len(2), ic_icc590.header_bytes);
    CHECK(!ic_knots_fits(&ic_icc590, 2));

    /* Profile 1 is 80 bytes like Siacoin but rolls the front of the buffer,
     * so no part in this family grinds the right sixteen bytes. Luke has said
     * it would be nice if someone started making ASICs for it. */
    CHECK_EQ(ic_knots_payload_len(1), 80);
    CHECK_EQ(ic_knots_roll_offset(1), 0);
    CHECK(!ic_knots_fits(&ic_ict580, 1));
    CHECK(!ic_knots_fits(&ic_ica586, 1));
    CHECK(!ic_knots_fits(&ic_icc590, 1));

    /* Nothing here is 160 bytes. */
    CHECK(!ic_knots_fits(&ic_ict580, 3));
    CHECK(!ic_knots_fits(&ic_icc590, 3));
    CHECK(!ic_knots_fits(&ic_ica586, 3));
}

static void nbits_to_target(void)
{
    uint8_t t[32], w[32];
    int i;

    /* Difficulty 1. */
    CHECK_EQ(ic_knots_target_be(0x1d00FFFFu, t), IC_OK);
    CHECK_EQ(t[0], 0x00);
    CHECK_EQ(t[3], 0x00);
    CHECK_EQ(t[4], 0xFF);
    CHECK_EQ(t[5], 0xFF);
    CHECK_EQ(t[6], 0x00);

    /* The work-payload order is the same target the other way up, so the eight
     * bytes ic_work_build puts in the trailer are the target's top 64 bits,
     * most significant first, which is what the chip compares. */
    CHECK_EQ(ic_knots_target_for_work(0x1d00FFFFu, w), IC_OK);
    for (i = 0; i < 32; i++)
        CHECK_EQ(w[i], t[31 - i]);

    CHECK_EQ(ic_knots_target_be(0x00800000u, t), IC_ERR_RANGE);   /* negative */
    CHECK_EQ(ic_knots_target_be(0xFF000001u, t), IC_ERR_RANGE);   /* overflows */
}

/* The trailer ic_work_build emits has to be the top 64 bits of the target. */
static void work_payload_carries_the_right_bound(void)
{
    struct ic_work w;
    uint8_t header[80], payload[160];
    int i, len;

    memset(&w, 0, sizeof(w));
    memset(header, 0, sizeof(header));
    w.header = header;
    w.header_len = 80;
    CHECK_EQ(ic_knots_target_for_work(0x1d00FFFFu, w.target), IC_OK);

    len = ic_work_build(&ic_ict580, &w, payload, sizeof(payload));
    CHECK_EQ(len, 96);

    {
        uint8_t be[32];

        CHECK_EQ(ic_knots_target_be(0x1d00FFFFu, be), IC_OK);
        for (i = 0; i < 8; i++)
            CHECK_EQ(payload[0x50 + i], be[i]);
    }
}

static void the_xor_key_decides_what_the_chip_can_see(void)
{
    size_t i;
    int exact_seen = 0, hidden_seen = 0;

    for (i = 0; i < KNOTS_NVECTORS; i++) {
        const struct knots_vector *v = &knots_vectors[i];
        int exact = ic_knots_asic_target_exact(&v->h);
        int top_clear = 1;
        int j;

        for (j = 0; j < 8; j++)
            if (v->mask[j])
                top_clear = 0;
        CHECK_EQ(exact, top_clear);
        if (exact) exact_seen = 1; else hidden_seen = 1;
    }
    /* The vectors cover both cases, so this is actually testing something. */
    CHECK(exact_seen);
    CHECK(hidden_seen);
}

TEST_MAIN("knots",
    every_stage_matches_the_node();
    rolled_fields_round_trip();
    a_result_lands_in_the_right_fields();
    profile_zero_is_the_sia_layout();
    nbits_to_target();
    work_payload_carries_the_right_bound();
    the_xor_key_decides_what_the_chip_can_see();
)
