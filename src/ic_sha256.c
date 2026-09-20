/* ic_sha256.c - SHA-256 and the tagged-hash wrapper Knots uses. */
#include <string.h>

#include "ic_crypto.h"

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, hh, t1, t2;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
               (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];

    for (i = 0; i < 64; i++) {
        t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

struct sha256_ctx {
    uint32_t h[8];
    uint8_t  buf[64];
    size_t   buflen;
    uint64_t total;
};

static void sha256_init(struct sha256_ctx *c)
{
    static const uint32_t iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    memcpy(c->h, iv, sizeof(iv));
    c->buflen = 0;
    c->total = 0;
}

static void sha256_update(struct sha256_ctx *c, const uint8_t *p, size_t len)
{
    c->total += len;
    if (c->buflen) {
        size_t take = 64 - c->buflen;

        if (take > len)
            take = len;
        memcpy(c->buf + c->buflen, p, take);
        c->buflen += take;
        p += take;
        len -= take;
        if (c->buflen == 64) {
            sha256_block(c->h, c->buf);
            c->buflen = 0;
        }
    }
    while (len >= 64) {
        sha256_block(c->h, p);
        p += 64;
        len -= 64;
    }
    if (len) {
        memcpy(c->buf, p, len);
        c->buflen = len;
    }
}

static void sha256_final(struct sha256_ctx *c, uint8_t out[32])
{
    uint64_t bits = c->total * 8;
    uint8_t pad[72];
    size_t padlen, i;

    padlen = (c->buflen < 56) ? (56 - c->buflen) : (120 - c->buflen);
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (i = 0; i < 8; i++)
        pad[padlen + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(c, pad, padlen + 8);

    for (i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(c->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->h[i]);
    }
}

void ic_sha256(void *out, const void *in, size_t inlen)
{
    struct sha256_ctx c;

    sha256_init(&c);
    sha256_update(&c, in, inlen);
    sha256_final(&c, out);
}

/* ---------------------------------------------------------- tagged hashes */

void ic_tagged_begin(struct ic_tagged *t, const char *tag)
{
    struct sha256_ctx c;
    uint8_t th[32];

    ic_sha256(th, tag, strlen(tag));

    /* The tag hash goes in twice, which is where the 0x40 extra bytes in the
     * assertions of block.cpp come from. */
    sha256_init(&c);
    sha256_update(&c, th, 32);
    sha256_update(&c, th, 32);

    memcpy(t->h, c.h, sizeof(t->h));
    memcpy(t->buf, c.buf, sizeof(t->buf));
    t->buflen = c.buflen;
    t->total = c.total;
}

void ic_tagged_write(struct ic_tagged *t, const void *data, size_t len)
{
    struct sha256_ctx c;

    memcpy(c.h, t->h, sizeof(c.h));
    memcpy(c.buf, t->buf, sizeof(c.buf));
    c.buflen = t->buflen;
    c.total = t->total;

    sha256_update(&c, data, len);

    memcpy(t->h, c.h, sizeof(t->h));
    memcpy(t->buf, c.buf, sizeof(t->buf));
    t->buflen = c.buflen;
    t->total = c.total;
}

void ic_tagged_u8(struct ic_tagged *t, uint8_t v)
{
    ic_tagged_write(t, &v, 1);
}

void ic_tagged_u16le(struct ic_tagged *t, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };

    ic_tagged_write(t, b, 2);
}

void ic_tagged_u32le(struct ic_tagged *t, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };

    ic_tagged_write(t, b, 4);
}

size_t ic_tagged_written(const struct ic_tagged *t)
{
    return (size_t)t->total;
}

void ic_tagged_end(struct ic_tagged *t, uint8_t out[32])
{
    struct sha256_ctx c;

    memcpy(c.h, t->h, sizeof(c.h));
    memcpy(c.buf, t->buf, sizeof(c.buf));
    c.buflen = t->buflen;
    c.total = t->total;
    sha256_final(&c, out);
}

void ic_tagged_hash(uint8_t out[32], const char *tag, const void *data, size_t len)
{
    struct ic_tagged t;

    ic_tagged_begin(&t, tag);
    ic_tagged_write(&t, data, len);
    ic_tagged_end(&t, out);
}
