/* intchains_knots.h - driving these chips with Bitcoin Knots BLAKE2b work.
 *
 * The chips were built for Siacoin and Handshake. Knots v2 headers are 164
 * bytes, which no part here can hash. It does not have to: the PoW is not over
 * the header. GetHash() folds the header down to a short buffer and hashes
 * that, and which buffer it builds is selected by the low two bits of m_flags.
 * Profile 0 comes out 80 bytes long with the fields the miner varies sitting
 * at offset 0x20, which is the Siacoin layout the ICT580 and ICA586 already
 * take, byte for byte.
 *
 *   profile 0   80 B   rolls 0x20   ICT580, ICA586
 *   profile 1   80 B   rolls 0x00   nothing in this family
 *   profile 2  128 B   rolls 0x50   ICC590 length, wrong roll offset
 *   profile 3  160 B   rolls 0x70   nothing in this family
 *
 * Verified against src/test/data/block_header_v2.json in the Knots tree: all
 * five vectors, both BLAKE2b stages, the asic_input buffer and the block hash.
 * Verified in software. No chip has been asked to hash any of it.
 *
 * Reference: src/primitives/block.cpp, CBlockHeader::GetHash().
 */
#ifndef INTCHAINS_KNOTS_H
#define INTCHAINS_KNOTS_H

#include <stddef.h>
#include <stdint.h>

#include "intchains.h"

#ifdef __cplusplus
extern "C" {
#endif

/* m_flags, from src/primitives/block.h */
#define IC_KNOTS_FLAG_USE_TIME_OFFSET 0x04
#define IC_KNOTS_PROFILE_MASK         0x03

/* The offset the chip rolls, identical on every part in Table 9-1: eight bytes
 * of nonce at 0x20 and eight of timestamp at 0x28. */
#define IC_CHIP_ROLL_OFFSET 0x20

/* Every multi-byte field below is in the byte order it is serialized in, which
 * is the order it appears in a raw block header. That is the reverse of how
 * block explorers print a hash. */
struct ic_knots_header {
    int32_t  version;             /* nVersion; the v2 flag is added for you */
    uint8_t  prev_block[32];
    int32_t  height;
    uint8_t  merkle_root[32];
    uint32_t ntime;
    uint32_t nbits;
    uint32_t nonce;               /* nNonce        \                        */
    uint32_t nonce2;              /* m_nonce2       |  the four the chip    */
    uint32_t time_offset;         /* m_time_offset  |  varies while it runs */
    uint32_t nonce3;              /* m_nonce3      /                        */
    uint8_t  extranonce[16];
    uint8_t  flags;               /* m_flags; bits 0-1 pick the profile */
    uint8_t  xor_key_clear_bits;  /* m_xor_key_mask_clear_bits */
    uint8_t  xor_key[16];
    uint8_t  mm_rhs[32];
    uint16_t txcount;
};

static inline int ic_knots_profile(const struct ic_knots_header *h)
{
    return h->flags & IC_KNOTS_PROFILE_MASK;
}

/* 80, 80, 128 or 160; 0 for a profile that does not exist. */
size_t ic_knots_payload_len(int profile);

/* Where the four varied fields start inside that buffer. */
size_t ic_knots_roll_offset(int profile);

/* ----------------------------------------------------------- intermediates */

/* The fields a mining machine never sees, so it cannot brick itself at some
 * future block version, time or difficulty. */
void ic_knots_h1(const struct ic_knots_header *h, uint8_t out[32]);
void ic_knots_h2(const uint8_t h1[32], const uint8_t mm_rhs[32], uint8_t out[32]);

/* The fields that do go out over Sv1, folded into one 32 byte value. */
void ic_knots_hash1(const uint8_t h2[32], const uint8_t extranonce[16],
                    uint8_t out[32]);

/* The mask the finished hash is XORed with. All zero unless the pool set a
 * key, in which case its top xor_key_clear_bits bits are cleared. */
void ic_knots_xor_mask(const uint8_t xor_key[16], uint8_t clear_bits,
                       uint8_t out[32]);

/* ------------------------------------------------------------- work buffer */

/* Build the buffer the chip hashes. Returns its length or IC_ERR_ARG. */
int ic_knots_payload(const struct ic_knots_header *h, uint8_t *out, size_t outsz);

/* Read the four varied fields back out after the chip has changed them, and
 * write them in. Profile 1 orders them differently, so go through these
 * rather than indexing the buffer by hand. */
void ic_knots_payload_get(const uint8_t *payload, int profile,
                          uint32_t *nonce, uint32_t *nonce2,
                          uint32_t *time_offset, uint32_t *nonce3);
void ic_knots_payload_set(uint8_t *payload, int profile,
                          uint32_t nonce, uint32_t nonce2,
                          uint32_t time_offset, uint32_t nonce3);

/* Apply a result the chain reported: the chip's 8 byte nonce covers the first
 * two fields and its 8 byte timestamp the other two. */
void ic_knots_apply_result(uint8_t *payload, int profile,
                           uint64_t chip_nonce, uint64_t chip_time);

/* --------------------------------------------------------------- the hash */

/* The raw BLAKE2b digest of the work buffer. This is what a chip computes and
 * what it compares; the node compares the masked value below. */
void ic_knots_digest(const uint8_t *payload, size_t len, uint8_t out[32]);

/* The proof-of-work value, most significant byte first, which is the order a
 * block explorer prints it and the order the chip compares in. */
void ic_knots_pow_hash(const uint8_t *payload, size_t len,
                       const uint8_t mask[32], uint8_t out[32]);

/* The leading 64 bits of a digest as a big-endian number. A chip reports a
 * nonce when this is at or under the bound it was given. */
uint64_t ic_knots_compare_value(const uint8_t digest[32]);

/* The bound to hand a chip, taken from the top 64 bits of the target. This is
 * the same eight bytes ic_work_build puts in the work payload trailer. */
uint64_t ic_knots_bound_from_target(const uint8_t target_be[32]);

/* The sixteen 64-bit message words of the padded BLAKE2b block, for driving a
 * simulation. Returns IC_ERR_ARG when the buffer needs more than one block,
 * which is every profile except 3. */
int ic_knots_message_words(const uint8_t *payload, size_t len, uint64_t m[16]);

/* nBits to a 256 bit target. _be is most significant byte first, to compare
 * against ic_knots_pow_hash. _for_work is the order ic_work_build wants. */
int ic_knots_target_be(uint32_t nbits, uint8_t target[32]);
int ic_knots_target_for_work(uint32_t nbits, uint8_t target[32]);

/* 1 when the hash is at or under the target. */
int ic_knots_meets_target(const uint8_t pow[32], const uint8_t target[32]);

/* ------------------------------------------------------------ chip fitting */

/* 1 when this part can hash this profile as-is: the payload has to be the
 * length the part takes, and the varied fields have to land where the part
 * rolls. */
int ic_knots_fits(const struct ic_family *fam, int profile);

/* 1 when the chip's own 64 bit comparison agrees with the node's.
 *
 * The chip compares its raw BLAKE2b output; the node compares that output
 * XORed with the mask. They agree only when the mask's top 64 bits are zero,
 * which needs either no XOR key or xor_key_clear_bits of at least 64. When
 * this returns 0 the chain is still useful, but it is reporting shares against
 * an unmasked bound and the host has to hash every one of them. That is the
 * point of the key: it stops a miner recognizing its own block. */
int ic_knots_asic_target_exact(const struct ic_knots_header *h);

#ifdef __cplusplus
}
#endif
#endif /* INTCHAINS_KNOTS_H */
