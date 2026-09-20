/* ic_blake2b.c - BLAKE2b, unkeyed.
 *
 * Written from RFC 7693. The Knots tree carries the BLAKE2 reference code in
 * C++; this is plain C so the library has no dependency on that tree, and
 * tests/test_knots.c pins it against the header vectors from it.
 */
#include <string.h>

#include "ic_crypto.h"

#define BLOCKBYTES 128

static const uint64_t IV[8] = {
    UINT64_C(0x6a09e667f3bcc908), UINT64_C(0xbb67ae8584caa73b),
    UINT64_C(0x3c6ef372fe94f82b), UINT64_C(0xa54ff53a5f1d36f1),
    UINT64_C(0x510e527fade682d1), UINT64_C(0x9b05688c2b3e6c1f),
    UINT64_C(0x1f83d9abfb41bd6b), UINT64_C(0x5be0cd19137e2179)
};

static const uint8_t SIGMA[12][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0 },
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 }
};

static uint64_t load64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;

    for (i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static void store64(uint8_t *p, uint64_t v)
{
    int i;

    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t ror64(uint64_t x, int n)
{
    return (x >> n) | (x << (64 - n));
}

#define G(a, b, c, d, x, y)                     \
    do {                                        \
        a = a + b + (x); d = ror64(d ^ a, 32);  \
        c = c + d;       b = ror64(b ^ c, 24);  \
        a = a + b + (y); d = ror64(d ^ a, 16);  \
        c = c + d;       b = ror64(b ^ c, 63);  \
    } while (0)

static void compress(uint64_t h[8], const uint8_t block[BLOCKBYTES],
                     uint64_t total, int last)
{
    uint64_t v[16], m[16];
    int i, r;

    for (i = 0; i < 16; i++)
        m[i] = load64(block + i * 8);
    for (i = 0; i < 8; i++)
        v[i] = h[i];
    for (i = 0; i < 8; i++)
        v[8 + i] = IV[i];

    v[12] ^= total;
    /* v[13] ^= 0: nothing here ever reaches 2^64 bytes. */
    if (last)
        v[14] = ~v[14];

    for (r = 0; r < 12; r++) {
        const uint8_t *s = SIGMA[r];

        G(v[0], v[4], v[8],  v[12], m[s[0]],  m[s[1]]);
        G(v[1], v[5], v[9],  v[13], m[s[2]],  m[s[3]]);
        G(v[2], v[6], v[10], v[14], m[s[4]],  m[s[5]]);
        G(v[3], v[7], v[11], v[15], m[s[6]],  m[s[7]]);
        G(v[0], v[5], v[10], v[15], m[s[8]],  m[s[9]]);
        G(v[1], v[6], v[11], v[12], m[s[10]], m[s[11]]);
        G(v[2], v[7], v[8],  v[13], m[s[12]], m[s[13]]);
        G(v[3], v[4], v[9],  v[14], m[s[14]], m[s[15]]);
    }

    for (i = 0; i < 8; i++)
        h[i] ^= v[i] ^ v[8 + i];
}

void ic_blake2b(void *out, size_t outlen, const void *in, size_t inlen)
{
    const uint8_t *p = in;
    uint64_t h[8];
    uint8_t block[BLOCKBYTES], digest[64];
    uint64_t total = 0;
    size_t i;

    if (outlen == 0 || outlen > 64)
        return;

    memcpy(h, IV, sizeof(h));
    h[0] ^= UINT64_C(0x01010000) ^ outlen;    /* no key, fanout 1, depth 1 */

    /* Every block but the last goes through with the running byte count; the
     * last one carries the finalization flag. An empty input is one padded
     * block, which is why the loop condition is > and not >=. */
    while (inlen > BLOCKBYTES) {
        total += BLOCKBYTES;
        compress(h, p, total, 0);
        p += BLOCKBYTES;
        inlen -= BLOCKBYTES;
    }

    memset(block, 0, sizeof(block));
    memcpy(block, p, inlen);
    total += inlen;
    compress(h, block, total, 1);

    for (i = 0; i < 8; i++)
        store64(digest + i * 8, h[i]);
    memcpy(out, digest, outlen);
}
