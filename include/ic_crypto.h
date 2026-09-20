/* ic_crypto.h - the two hashes the Knots proof-of-work needs.
 *
 * A replacement controller cannot avoid these. The chip compares only the top
 * 64 bits of its own BLAKE2b output, so every result it reports has to be
 * hashed again by the host before it is worth sending anywhere.
 */
#ifndef IC_CRYPTO_H
#define IC_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/* BLAKE2b, unkeyed, arbitrary digest length up to 64. */
void ic_blake2b(void *out, size_t outlen, const void *in, size_t inlen);

/* SHA-256. */
void ic_sha256(void *out, const void *in, size_t inlen);

/* BIP340-style tagged hash: SHA256(SHA256(tag) || SHA256(tag) || data). */
void ic_tagged_hash(uint8_t out[32], const char *tag,
                    const void *data, size_t len);

/* Incremental tagged hash, for the streams built out of many fields. */
struct ic_tagged {
    uint8_t  buf[64];
    size_t   buflen;
    uint32_t h[8];
    uint64_t total;
};

void ic_tagged_begin(struct ic_tagged *t, const char *tag);
void ic_tagged_write(struct ic_tagged *t, const void *data, size_t len);
void ic_tagged_u8(struct ic_tagged *t, uint8_t v);
void ic_tagged_u16le(struct ic_tagged *t, uint16_t v);
void ic_tagged_u32le(struct ic_tagged *t, uint32_t v);
void ic_tagged_end(struct ic_tagged *t, uint8_t out[32]);
size_t ic_tagged_written(const struct ic_tagged *t);

#endif
