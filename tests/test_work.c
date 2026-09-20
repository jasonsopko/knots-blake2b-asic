#include "intchains.h"
#include "ic_test.h"

static uint8_t header[128];
static uint8_t target[32];

static struct ic_work make_work(size_t header_len)
{
    struct ic_work w;
    size_t i;

    memset(&w, 0, sizeof(w));
    memset(header, 0xAA, sizeof(header));
    for (i = 0; i < sizeof(target); i++)
        target[i] = (uint8_t)i;

    w.header      = header;
    w.header_len  = header_len;
    w.nonce_start = UINT64_C(0x0102030405060708);
    w.timestamp   = UINT64_C(0x00000000689A1B2C);
    memcpy(w.target, target, sizeof(target));
    return w;
}

static void common_head(const uint8_t *out)
{
    /* Header verbatim, then the nonce and the timestamp written over it. */
    CHECK_EQ(out[0x00], 0xAA);
    CHECK_EQ(out[0x1F], 0xAA);
    CHECK_EQ(out[0x20], 0x08);            /* nonce, little-endian */
    CHECK_EQ(out[0x27], 0x01);
    CHECK_EQ(out[0x28], 0x2C);            /* timestamp, little-endian */
    CHECK_EQ(out[0x2B], 0x68);
    CHECK_EQ(out[0x30], 0xAA);            /* header block 2 back to verbatim */
}

static void target_at(const uint8_t *out, size_t trailer)
{
    /* High word from target bytes [28:32], low from [24:28], each reversed. */
    CHECK_EQ(out[trailer + 0], 31);
    CHECK_EQ(out[trailer + 1], 30);
    CHECK_EQ(out[trailer + 2], 29);
    CHECK_EQ(out[trailer + 3], 28);
    CHECK_EQ(out[trailer + 4], 27);
    CHECK_EQ(out[trailer + 5], 26);
    CHECK_EQ(out[trailer + 6], 25);
    CHECK_EQ(out[trailer + 7], 24);
}

static void ict580_layout(void)
{
    struct ic_work w = make_work(80);
    uint8_t out[160];

    CHECK_EQ(ic_work_build(&ic_ict580, &w, out, sizeof(out)), 96);
    common_head(out);
    target_at(out, 0x50);

    /* Nonce ceiling 0x0000FFFFFFFFFFFF at 0x58. */
    CHECK_EQ(out[0x58], 0xFF);
    CHECK_EQ(out[0x5D], 0xFF);
    CHECK_EQ(out[0x5E], 0x00);
    CHECK_EQ(out[0x5F], 0x00);
}

static void icc590_layout(void)
{
    struct ic_work w = make_work(128);
    uint8_t out[160];

    CHECK_EQ(ic_work_build(&ic_icc590, &w, out, sizeof(out)), 144);
    common_head(out);
    CHECK_EQ(out[0x50], 0xAA);            /* header block 3 is still header */
    CHECK_EQ(out[0x7F], 0xAA);
    target_at(out, 0x80);
    CHECK_EQ(out[0x88], 0xFF);
    CHECK_EQ(out[0x8E], 0x00);
}

static void ica586_layout(void)
{
    struct ic_work w = make_work(80);
    uint8_t out[160];

    CHECK_EQ(ic_work_build(&ic_ica586, &w, out, sizeof(out)), 112);
    common_head(out);

    /* 0x50 to 0x5F is reserved and goes out as zero. */
    CHECK_EQ(out[0x50], 0x00);
    CHECK_EQ(out[0x5F], 0x00);

    /* The target is written at 0x60, then 0x13 and 0xDF land on top of its
     * first two bytes. Section 9, speculative note. */
    CHECK_EQ(out[0x60], 0x13);
    CHECK_EQ(out[0x61], 0xDF);
    CHECK_EQ(out[0x62], 29);
    CHECK_EQ(out[0x63], 28);
    CHECK_EQ(out[0x64], 27);
    CHECK_EQ(out[0x67], 24);

    /* 46 bit ceiling, not 48. */
    CHECK_EQ(out[0x68], 0xFF);
    CHECK_EQ(out[0x6C], 0xFF);
    CHECK_EQ(out[0x6D], 0x3F);
    CHECK_EQ(out[0x6E], 0x00);
}

/* The quirk has to follow the part, not the address of the struct: the API
 * takes a const pointer and a caller may well hand us their own copy. */
static void the_quirk_survives_a_copied_family(void)
{
    struct ic_family copy = ic_ica586;
    struct ic_work w = make_work(80);
    uint8_t a[160], b[160];

    CHECK_EQ(ic_work_build(&ic_ica586, &w, a, sizeof(a)), 112);
    CHECK_EQ(ic_work_build(&copy, &w, b, sizeof(b)), 112);
    CHECK(memcmp(a, b, 112) == 0);
    CHECK_EQ(b[0x60], 0x13);
    CHECK_EQ(b[0x61], 0xDF);

    /* And a part without the quirk keeps its target bytes. */
    CHECK_EQ(ic_ict580.quirks, 0);
    CHECK_EQ(ic_icc590.quirks, 0);
}

static void rejects_the_wrong_header_length(void)
{
    struct ic_work w = make_work(80);
    uint8_t out[160];

    CHECK_EQ(ic_work_build(&ic_icc590, &w, out, sizeof(out)), IC_ERR_ARG);

    w.header_len = 128;
    CHECK_EQ(ic_work_build(&ic_ict580, &w, out, sizeof(out)), IC_ERR_ARG);
}

static void refuses_the_unbound_part(void)
{
    struct ic_work w = make_work(80);
    uint8_t out[160];

    /* No product binds ICC551, so its nonce ceiling exists nowhere. */
    CHECK_EQ(ic_work_build(&ic_icc551, &w, out, sizeof(out)), IC_ERR_ARG);
}

static void payload_fits_the_packet_count(void)
{
    CHECK_EQ(ic_ict580.work_bytes, ic_ict580.work_pkt * 2);
    CHECK_EQ(ic_icc590.work_bytes, ic_icc590.work_pkt * 2);
    CHECK_EQ(ic_ica586.work_bytes, ic_ica586.work_pkt * 2);
    CHECK(ic_icc590.work_pkt <= IC_MAX_PAYLOAD_PKT);
}

TEST_MAIN("work",
    ict580_layout();
    icc590_layout();
    ica586_layout();
    the_quirk_survives_a_copied_family();
    rejects_the_wrong_header_length();
    refuses_the_unbound_part();
    payload_fits_the_packet_count();
)
