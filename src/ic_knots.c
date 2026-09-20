/* ic_knots.c - Bitcoin Knots BLAKE2b work for these chips.
 *
 * Follows src/primitives/block.cpp, CBlockHeader::GetHash(), stage by stage.
 */
#include <string.h>

#include "intchains_knots.h"
#include "ic_crypto.h"

#define VERSION_HEADER_V2_FLAG 0x80000000u

static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void reverse32(uint8_t out[32], const uint8_t in[32])
{
    int i;

    for (i = 0; i < 32; i++)
        out[i] = in[31 - i];
}

size_t ic_knots_payload_len(int profile)
{
    switch (profile) {
    case 0: return 80;
    case 1: return 80;
    case 2: return 128;
    case 3: return 160;
    default: return 0;
    }
}

size_t ic_knots_roll_offset(int profile)
{
    switch (profile) {
    case 0: return 0x20;
    case 1: return 0x00;
    case 2: return 0x50;
    case 3: return 0x70;
    default: return 0;
    }
}

/* --------------------------------------------------------- the two stages */

void ic_knots_h1(const struct ic_knots_header *h, uint8_t out[32])
{
    struct ic_tagged t;
    uint8_t xor_key_hash[32], prev_sane[32];
    uint32_t version, time_on_wire;

    ic_tagged_hash(xor_key_hash, "Bitcoin block hash PoW XOR key", h->xor_key, 16);

    version = VERSION_HEADER_V2_FLAG | ((uint32_t)h->version & ~VERSION_HEADER_V2_FLAG);

    /* The wire carries nTime minus the offset, so a chip rolling the offset
     * does not move the block's stated time. */
    time_on_wire = (h->flags & IC_KNOTS_FLAG_USE_TIME_OFFSET)
                       ? h->ntime - h->time_offset          /* wraps, as intended */
                       : h->ntime;

    reverse32(prev_sane, h->prev_block);

    ic_tagged_begin(&t, "Bitcoin block header 1");
    ic_tagged_u32le(&t, version);
    ic_tagged_write(&t, prev_sane, 32);
    ic_tagged_u32le(&t, (uint32_t)h->height);
    ic_tagged_write(&t, h->merkle_root, 32);
    ic_tagged_u32le(&t, time_on_wire);
    ic_tagged_u8(&t, 0);                      /* reserved for 40 bit time */
    ic_tagged_u32le(&t, h->nbits);
    ic_tagged_u32le(&t, (uint32_t)h->txcount);
    ic_tagged_u8(&t, h->flags);
    ic_tagged_u8(&t, h->xor_key_clear_bits);
    ic_tagged_write(&t, xor_key_hash, 32);
    ic_tagged_end(&t, out);
}

void ic_knots_h2(const uint8_t h1[32], const uint8_t mm_rhs[32], uint8_t out[32])
{
    static const uint8_t zeros[32] = { 0 };
    struct ic_tagged t;

    ic_tagged_begin(&t, "Merge-mining hook");
    ic_tagged_write(&t, h1, 32);
    ic_tagged_write(&t, zeros, 32);           /* two uint128 of nothing */
    ic_tagged_write(&t, mm_rhs, 32);
    ic_tagged_end(&t, out);
}

void ic_knots_hash1(const uint8_t h2[32], const uint8_t extranonce[16],
                    uint8_t out[32])
{
    uint8_t ss[52];

    /* Exactly what goes out over Sv1: three spare bytes of coinb1, then the
     * merge-mining hook, then the extranonce. */
    memset(ss, 0, 4);
    memcpy(ss + 4, h2, 32);
    memcpy(ss + 36, extranonce, 16);
    ic_blake2b(out, 32, ss, sizeof(ss));
}

void ic_knots_xor_mask(const uint8_t xor_key[16], uint8_t clear_bits,
                       uint8_t out[32])
{
    unsigned clear_bytes;
    int i, any = 0;

    for (i = 0; i < 16; i++)
        any |= xor_key[i];
    if (!any) {
        memset(out, 0, 32);
        return;
    }

    ic_tagged_hash(out, "Bitcoin block hash PoW XOR mask", xor_key, 16);

    /* The cleared bits are the ones the miner is allowed to see. */
    clear_bytes = (unsigned)clear_bits / 8u;
    memset(out, 0, clear_bytes);
    out[clear_bytes] &= (uint8_t)(0xFFu >> (clear_bits % 8u));
}

/* ------------------------------------------------------------- work buffer */

int ic_knots_payload(const struct ic_knots_header *h, uint8_t *out, size_t outsz)
{
    uint8_t h1[32], h2[32], hash1[32], prev_hidden[32], prev_sane[32];
    int profile;
    size_t len, roll;

    if (!h || !out)
        return IC_ERR_ARG;
    profile = ic_knots_profile(h);
    len = ic_knots_payload_len(profile);
    if (!len || outsz < len)
        return IC_ERR_ARG;

    ic_knots_h1(h, h1);
    ic_knots_h2(h1, h->mm_rhs, h2);
    ic_knots_hash1(h2, h->extranonce, hash1);

    memset(out, 0, len);
    roll = ic_knots_roll_offset(profile);

    switch (profile) {
    case 0:
        reverse32(prev_sane, h->prev_block);
        ic_tagged_hash(prev_hidden, "Bitcoin prevblock header, hashed", prev_sane, 32);
        /* The top six bytes go, so the chip cannot tell which chain it is on
         * from the parent alone. */
        memset(prev_hidden, 0, 6);
        memcpy(out, prev_hidden, 32);
        memcpy(out + 0x30, hash1, 32);
        break;
    case 1:
        memcpy(out + 0x10, hash1, 32);
        memcpy(out + 0x30, h2, 32);
        break;
    case 2:
        memcpy(out + 0x30, h2, 32);
        memcpy(out + 0x60, hash1, 32);
        break;
    case 3:
        memcpy(out + 0x50, h2, 32);
        memcpy(out + 0x80, hash1, 32);
        break;
    default:
        return IC_ERR_ARG;
    }
    (void)roll;

    ic_knots_payload_set(out, profile, h->nonce, h->nonce2, h->time_offset, h->nonce3);
    return (int)len;
}

void ic_knots_payload_get(const uint8_t *payload, int profile,
                          uint32_t *nonce, uint32_t *nonce2,
                          uint32_t *time_offset, uint32_t *nonce3)
{
    const uint8_t *p = payload + ic_knots_roll_offset(profile);

    /* Profile 1 puts nonce3 before the time offset; everything else does not. */
    if (nonce)       *nonce = get_u32le(p);
    if (nonce2)      *nonce2 = get_u32le(p + 4);
    if (profile == 1) {
        if (nonce3)      *nonce3 = get_u32le(p + 8);
        if (time_offset) *time_offset = get_u32le(p + 12);
    } else {
        if (time_offset) *time_offset = get_u32le(p + 8);
        if (nonce3)      *nonce3 = get_u32le(p + 12);
    }
}

void ic_knots_payload_set(uint8_t *payload, int profile,
                          uint32_t nonce, uint32_t nonce2,
                          uint32_t time_offset, uint32_t nonce3)
{
    uint8_t *p = payload + ic_knots_roll_offset(profile);

    put_u32le(p, nonce);
    put_u32le(p + 4, nonce2);
    if (profile == 1) {
        put_u32le(p + 8, nonce3);
        put_u32le(p + 12, time_offset);
    } else {
        put_u32le(p + 8, time_offset);
        put_u32le(p + 12, nonce3);
    }
}

void ic_knots_apply_result(uint8_t *payload, int profile,
                           uint64_t chip_nonce, uint64_t chip_time)
{
    uint8_t *p = payload + ic_knots_roll_offset(profile);
    int i;

    /* The chip's eight byte nonce sits over the first two fields and its eight
     * byte timestamp over the other two, little-endian in both cases. */
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)(chip_nonce >> (8 * i));
        p[8 + i] = (uint8_t)(chip_time >> (8 * i));
    }
}

/* --------------------------------------------------------------- the hash */

void ic_knots_digest(const uint8_t *payload, size_t len, uint8_t out[32])
{
    ic_blake2b(out, 32, payload, len);
}

void ic_knots_pow_hash(const uint8_t *payload, size_t len,
                       const uint8_t mask[32], uint8_t out[32])
{
    uint8_t hash[32];
    int i;

    ic_knots_digest(payload, len, hash);
    for (i = 0; i < 32; i++)
        out[i] = (uint8_t)(hash[i] ^ (mask ? mask[i] : 0));
}

uint64_t ic_knots_compare_value(const uint8_t digest[32])
{
    uint64_t v = 0;
    int i;

    /* Byte 0 of the digest is the most significant byte of the value, so this
     * is the first chaining word out of the compression, byte-swapped. */
    for (i = 0; i < 8; i++)
        v = v << 8 | digest[i];
    return v;
}

uint64_t ic_knots_bound_from_target(const uint8_t target_be[32])
{
    return ic_knots_compare_value(target_be);   /* same eight leading bytes */
}

int ic_knots_message_words(const uint8_t *payload, size_t len, uint64_t m[16])
{
    uint8_t block[128];
    int i, j;

    if (!payload || !m || len > sizeof(block))
        return IC_ERR_ARG;

    memset(block, 0, sizeof(block));
    memcpy(block, payload, len);

    for (i = 0; i < 16; i++) {
        uint64_t w = 0;

        for (j = 7; j >= 0; j--)
            w = w << 8 | block[i * 8 + j];
        m[i] = w;
    }
    return IC_OK;
}

int ic_knots_target_be(uint32_t nbits, uint8_t target[32])
{
    unsigned exponent = nbits >> 24;
    uint32_t mantissa = nbits & 0x007FFFFFu;

    if (!target)
        return IC_ERR_ARG;
    memset(target, 0, 32);

    if (nbits & 0x00800000u)      /* a negative target is not a thing */
        return IC_ERR_RANGE;
    if (mantissa == 0)
        return IC_OK;             /* target zero: nothing can satisfy it */

    if (exponent <= 3) {
        mantissa >>= 8 * (3 - exponent);
        target[31] = (uint8_t)mantissa;
        target[30] = (uint8_t)(mantissa >> 8);
        target[29] = (uint8_t)(mantissa >> 16);
        return IC_OK;
    }

    /* The mantissa's low byte sits at 256^(exponent-3), which in a big-endian
     * buffer is index 34 - exponent. Overflow is a property of the value, not
     * of the exponent: an exponent of 33 or 34 is fine as long as the bytes
     * that fall off the top are zero. That is what SetCompact does. */
    {
        uint8_t byte[3];
        int idx[3], k;

        byte[0] = (uint8_t)mantissa;
        byte[1] = (uint8_t)(mantissa >> 8);
        byte[2] = (uint8_t)(mantissa >> 16);
        idx[0] = 34 - (int)exponent;
        idx[1] = 33 - (int)exponent;
        idx[2] = 32 - (int)exponent;

        for (k = 0; k < 3; k++) {
            if (idx[k] < 0) {
                if (byte[k])
                    return IC_ERR_RANGE;   /* will not fit in 256 bits */
                continue;
            }
            target[idx[k]] = byte[k];
        }
    }
    return IC_OK;
}

int ic_knots_target_for_work(uint32_t nbits, uint8_t target[32])
{
    uint8_t be[32];
    int rc = ic_knots_target_be(nbits, be);

    if (rc != IC_OK)
        return rc;
    reverse32(target, be);       /* ic_work_build wants it the other way up */
    return IC_OK;
}

int ic_knots_meets_target(const uint8_t pow[32], const uint8_t target[32])
{
    return memcmp(pow, target, 32) <= 0;
}

/* ------------------------------------------------------------ chip fitting */

int ic_knots_fits(const struct ic_family *fam, int profile)
{
    if (!fam)
        return 0;
    /* The header the part takes has to be the length this profile produces,
     * and the fields the chip varies have to be the fields the profile put
     * there. A part that takes the right length but rolls the wrong bytes is
     * not a fit: it would grind through something that is not a nonce. */
    return ic_knots_payload_len(profile) == fam->header_bytes &&
           ic_knots_roll_offset(profile) == IC_CHIP_ROLL_OFFSET;
}

int ic_knots_asic_target_exact(const struct ic_knots_header *h)
{
    uint8_t mask[32];
    int i;

    if (!h)
        return 0;
    ic_knots_xor_mask(h->xor_key, h->xor_key_clear_bits, mask);
    for (i = 0; i < 8; i++)
        if (mask[i])
            return 0;
    return 1;
}
