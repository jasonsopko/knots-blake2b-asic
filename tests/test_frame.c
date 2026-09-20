#include "intchains.h"
#include "ic_test.h"

static void builds_the_enumerate_frame(void)
{
    /* Section 7.2:  TX   A5 3C | 02 00 | 00 00 | 00 00 */
    const uint8_t want[] = { 0xA5, 0x3C, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint16_t payload[1] = { 0x0000 };
    uint8_t out[32];
    int n;

    n = ic_frame_build(out, sizeof(out), IC_CMD(0, IC_OP_AUTOADDR, 0), payload, 1, 0);
    CHECK_EQ(n, (int)sizeof(want));
    CHECK(memcmp(out, want, sizeof(want)) == 0);
}

static void packets_go_out_msb_first(void)
{
    uint16_t payload[1] = { 0x00E5 };
    uint8_t out[32];

    CHECK_EQ(ic_frame_build(out, sizeof(out), 0x0400, payload, 1, 0), 8);
    CHECK_EQ(out[2], 0x04);
    CHECK_EQ(out[3], 0x00);
    CHECK_EQ(out[4], 0x00);
    CHECK_EQ(out[5], 0xE5);
}

static void crc_covers_preamble_command_payload(void)
{
    uint16_t payload[3] = { 0x0007, 0x1234, 0x5678 };
    uint8_t out[32];
    uint16_t want;
    int n;

    n = ic_frame_build(out, sizeof(out), IC_CMD(0, IC_OP_WRITEREG, 1), payload, 3, 1);
    CHECK_EQ(n, 14);                       /* preamble cmd 3 payload crc tail */
    want = ic_crc16_wire(out, 10);         /* preamble, command, payload only */
    CHECK_EQ(out[10], (want >> 8) & 0xFF);
    CHECK_EQ(out[11], want & 0xFF);
    CHECK_EQ(out[12], 0x00);               /* the tail is outside the CRC */
    CHECK_EQ(out[13], 0x00);
}

static void refuses_to_overflow(void)
{
    uint8_t out[4];

    CHECK_EQ(ic_frame_build(out, sizeof(out), 0x0200, NULL, 0, 0), IC_ERR_ARG);
}

static void finds_a_reply_at_any_offset(void)
{
    struct ic_tmpl t = { 0x0200, 0xFFFF };
    struct ic_reply r;
    uint8_t rx[40];
    int n;

    memset(rx, 0, sizeof(rx));
    rx[3] = 0xA5;                          /* a stray byte before the reply */
    n = ic_frame_build(rx + 11, sizeof(rx) - 11, 0x0200, (uint16_t[]){ 0x0054 }, 1, 0);
    CHECK(n > 0);

    CHECK_EQ(ic_frame_find(rx, sizeof(rx), &t, 1, 1, 0, &r), IC_OK);
    CHECK_EQ(r.offset, 11);
    CHECK_EQ(r.cmd, 0x0200);
    CHECK_EQ(r.pkt[0], 0x0054);            /* 84 chips */
}

static void rejects_a_wrong_command_word(void)
{
    struct ic_tmpl t = { 0x0200, 0xFFFF };
    struct ic_reply r;
    uint8_t rx[16];

    memset(rx, 0, sizeof(rx));
    ic_frame_build(rx, sizeof(rx), 0x0300, (uint16_t[]){ 0 }, 1, 0);
    CHECK_EQ(ic_frame_find(rx, sizeof(rx), &t, 1, 1, 0, &r), IC_ERR_NOREPLY);
}

static void reports_a_bad_checksum(void)
{
    struct ic_tmpl t = { 0x1A00, 0xFF00 };
    struct ic_reply r;
    uint8_t rx[24];

    memset(rx, 0, sizeof(rx));
    ic_frame_build(rx, sizeof(rx), 0x1A01, (uint16_t[]){ 0, 0xDEAD, 0xBEEF }, 3, 1);
    CHECK_EQ(ic_frame_find(rx, sizeof(rx), &t, 1, 3, 1, &r), IC_OK);
    CHECK_EQ(r.pkt[1], 0xDEAD);

    rx[10] ^= 0xFF;                        /* corrupt the checksum's high byte */
    CHECK_EQ(ic_frame_find(rx, sizeof(rx), &t, 1, 3, 1, &r), IC_ERR_CRC);
}

/* The chain buffer has to hold the worst case, which is the far end of a
 * fully addressed chain, not the broadcast fill. */
static void the_buffer_holds_the_furthest_chip(void)
{
    size_t frame = ic_frame_len(1, 0);
    size_t reply = ic_frame_len(3, 1);

    CHECK(frame + ic_filler_bytes(255, frame, reply) <= IC_MAX_XFER_BYTES);
    CHECK(frame + ic_filler_bytes(IC_BROADCAST, frame, reply) <= IC_MAX_XFER_BYTES);

    /* And the longest frame this family has, broadcast. */
    frame = ic_frame_len(IC_MAX_PAYLOAD_PKT, 1);
    CHECK(frame + ic_filler_bytes(IC_BROADCAST, frame, frame) <= IC_MAX_XFER_BYTES);
    CHECK(frame + ic_filler_bytes(255, frame, frame) <= IC_MAX_XFER_BYTES);
}

static void turnaround_is_six_bytes_per_hop(void)
{
    /* Section 6. Frame and reply lengths equal, so no extra. */
    CHECK_EQ(ic_filler_bytes(1, 8, 8), 6);
    CHECK_EQ(ic_filler_bytes(84, 8, 8), 504);
    CHECK_EQ(ic_filler_bytes(IC_BROADCAST, 8, 8), 900);

    /* A longer reply adds the difference on top. */
    CHECK_EQ(ic_filler_bytes(2, 8, 20), 12 + 12);
    CHECK_EQ(ic_filler_bytes(IC_BROADCAST, 8, 20), 912);

    /* The longest chain in the family still fits inside the broadcast fill. */
    CHECK(96 * IC_FILLER_PER_HOP <= IC_BROADCAST_FILLER);
}

TEST_MAIN("frame",
    builds_the_enumerate_frame();
    packets_go_out_msb_first();
    crc_covers_preamble_command_payload();
    refuses_to_overflow();
    finds_a_reply_at_any_offset();
    rejects_a_wrong_command_word();
    reports_a_bad_checksum();
    turnaround_is_six_bytes_per_hop();
    the_buffer_holds_the_furthest_chip();
)
