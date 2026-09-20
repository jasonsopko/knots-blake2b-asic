/* ic-grind - find a nonce for a v2 block header, any profile.
 *
 * Takes a serialized 164-byte header and a 256-bit target, varies the fields a
 * mining chip would vary, and prints the header that satisfies the target.
 * The proof of work comes from the library, so a header this produces is one
 * built to docs/profile-1.md rather than to a second implementation of it.
 *
 *   ic-grind <header-hex> <target-hex> [max-tries]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intchains_knots.h"

#define HDR 164

static int unhex(const char *s, uint8_t *o, size_t n)
{
    size_t i;
    unsigned v;

    if (strlen(s) != n * 2)
        return -1;
    for (i = 0; i < n; i++) {
        if (sscanf(s + i * 2, "%2x", &v) != 1)
            return -1;
        o[i] = (uint8_t)v;
    }
    return 0;
}

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Same field order as ic-verify-block: this is the wire header. */
static void parse(const uint8_t *h, struct ic_knots_header *out)
{
    uint32_t time_on_wire;

    memset(out, 0, sizeof(*out));
    out->version = (int32_t)u32le(h + 0);
    memcpy(out->prev_block, h + 4, 32);
    memcpy(out->merkle_root, h + 36, 32);
    time_on_wire = u32le(h + 68);
    out->nbits  = u32le(h + 72);
    out->nonce  = u32le(h + 76);
    out->nonce2 = u32le(h + 80);
    out->nonce3 = u32le(h + 84);
    memcpy(out->extranonce, h + 88, 16);
    out->time_offset = u32le(h + 104);
    out->txcount = (uint16_t)(h[108] | (uint16_t)h[109] << 8);
    out->flags = h[110];
    out->xor_key_clear_bits = h[111];
    memcpy(out->xor_key, h + 112, 16);
    out->height = (int32_t)u32le(h + 128);
    memcpy(out->mm_rhs, h + 132, 32);
    out->ntime = time_on_wire;
    if (out->flags & IC_KNOTS_FLAG_USE_TIME_OFFSET)
        out->ntime = time_on_wire + out->time_offset;
}

int main(int argc, char **argv)
{
    uint8_t raw[HDR], target[32], payload[160], mask[32], pow[32];
    struct ic_knots_header h;
    unsigned long long tries, max = 100000000ull;
    int profile, len, i;

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: ic-grind <header-hex> <target-hex> [max-tries]\n");
        return 2;
    }
    if (unhex(argv[1], raw, HDR) != 0) {
        fprintf(stderr, "header must be %d bytes of hex\n", HDR);
        return 2;
    }
    if (unhex(argv[2], target, 32) != 0) {
        fprintf(stderr, "target must be 32 bytes of hex, most significant first\n");
        return 2;
    }
    if (argc == 4)
        max = strtoull(argv[3], NULL, 10);

    parse(raw, &h);
    profile = ic_knots_profile(&h);
    ic_knots_xor_mask(h.xor_key, h.xor_key_clear_bits, mask);

    fprintf(stderr, "grinding profile %d, height %d, %zu byte buffer\n",
            profile, h.height, ic_knots_payload_len(profile));

    /* The chip varies an eight byte nonce; nNonce and m_nonce2 are what sit
     * under it on every profile. */
    for (tries = 0; tries < max; tries++) {
        h.nonce  = (uint32_t)tries;
        h.nonce2 = (uint32_t)(tries >> 32);

        len = ic_knots_payload(&h, payload, sizeof(payload));
        if (len < 0) {
            fprintf(stderr, "payload: %s\n", ic_strerror(len));
            return 1;
        }
        ic_knots_pow_hash(payload, (size_t)len, mask, pow);

        if (ic_knots_meets_target(pow, target)) {
            put_u32le(raw + 76, h.nonce);
            put_u32le(raw + 80, h.nonce2);
            fprintf(stderr, "found after %llu tries: ", tries + 1);
            for (i = 0; i < 32; i++) fprintf(stderr, "%02x", pow[i]);
            fprintf(stderr, "\n");
            for (i = 0; i < HDR; i++) printf("%02x", raw[i]);
            printf("\n");
            return 0;
        }
    }

    fprintf(stderr, "no nonce found in %llu tries\n", max);
    return 1;
}
