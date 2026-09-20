/* ic-verify-block - check the library against a real block.
 *
 * Takes a serialized v2 block header and the hash the chain gives it, rebuilds
 * the buffer a mining chip would have hashed, and compares. Test vectors say
 * we agree with the node's test data; this says we agree with the chain.
 *
 *   curl -s http://127.0.0.1:8332/rest/headers/1/$HASH.hex | \
 *       xargs build/ic-verify-block $HASH
 */
#include <stdio.h>
#include <string.h>

#include "intchains_knots.h"

#define HEADER_V2_BYTES 164

static int unhex(const char *s, uint8_t *out, size_t want)
{
    size_t i;

    if (strlen(s) != want * 2)
        return -1;
    for (i = 0; i < want; i++) {
        unsigned v;

        if (sscanf(s + i * 2, "%2x", &v) != 1)
            return -1;
        out[i] = (uint8_t)v;
    }
    return 0;
}

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The wire order is version, prev, merkle, time, bits, nonce, then the v2
 * tail: nonce2, nonce3, extranonce, time_offset, txcount, flags,
 * xor_key_mask_clear_bits, xor_key, height, mm_rhs. */
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

    /* The wire carries the time with the offset already taken off. */
    out->ntime = time_on_wire;
    if (out->flags & IC_KNOTS_FLAG_USE_TIME_OFFSET)
        out->ntime = time_on_wire + out->time_offset;
}

/* Returns 0 when the rebuilt proof of work matches. */
static int check(const char *hash_hex, const char *header_hex,
                 int *profile_out, int *masked_out)
{
    uint8_t raw[HEADER_V2_BYTES], want[32], payload[160], mask[32], pow[32];
    struct ic_knots_header h;
    int len;

    if (unhex(hash_hex, want, 32) != 0 ||
        unhex(header_hex, raw, HEADER_V2_BYTES) != 0)
        return -1;

    parse(raw, &h);
    len = ic_knots_payload(&h, payload, sizeof(payload));
    if (len < 0)
        return -1;

    ic_knots_xor_mask(h.xor_key, h.xor_key_clear_bits, mask);
    ic_knots_pow_hash(payload, (size_t)len, mask, pow);

    if (profile_out)
        *profile_out = ic_knots_profile(&h);
    if (masked_out)
        *masked_out = !ic_knots_asic_target_exact(&h);

    return memcmp(pow, want, 32) == 0 ? 0 : 1;
}

/* Read "<blockhash> <header-hex>" lines and tally. */
static int batch(void)
{
    char line[1024], hash_hex[128], header_hex[512];
    long ok = 0, bad = 0, unreadable = 0, masked = 0;
    long prof[4] = { 0, 0, 0, 0 };

    while (fgets(line, sizeof(line), stdin)) {
        int p = -1, m = 0, rc;

        if (sscanf(line, "%127s %511s", hash_hex, header_hex) != 2)
            continue;
        rc = check(hash_hex, header_hex, &p, &m);
        if (rc < 0) {
            unreadable++;
        } else if (rc == 0) {
            ok++;
            if (p >= 0 && p < 4) prof[p]++;
            masked += m;
        } else {
            bad++;
            printf("MISMATCH %s\n", hash_hex);
        }
    }

    printf("verified %ld blocks against the chain, %ld mismatched, %ld unreadable\n",
           ok, bad, unreadable);
    printf("  profile 0 %ld, 1 %ld, 2 %ld, 3 %ld\n", prof[0], prof[1], prof[2], prof[3]);
    printf("  %ld had an XOR mask hiding the target from the chip\n", masked);
    return bad || unreadable ? 1 : 0;
}

int main(int argc, char **argv)
{
    uint8_t raw[HEADER_V2_BYTES], want[32], payload[160], mask[32], pow[32];
    struct ic_knots_header h;
    uint64_t bound;
    uint8_t target[32];
    int len, profile, i;

    if (argc == 2 && !strcmp(argv[1], "-"))
        return batch();

    if (argc != 3) {
        fprintf(stderr, "usage: ic-verify-block <blockhash> <header-hex>\n"
                        "       ic-verify-block -      (lines of hash and hex on stdin)\n");
        return 2;
    }
    if (unhex(argv[1], want, 32) != 0) {
        fprintf(stderr, "block hash must be 64 hex characters\n");
        return 2;
    }
    if (unhex(argv[2], raw, HEADER_V2_BYTES) != 0) {
        fprintf(stderr, "header must be %d bytes of hex; a v1 header is 80 and "
                        "has no proof of work we can rebuild\n", HEADER_V2_BYTES);
        return 2;
    }

    parse(raw, &h);
    profile = ic_knots_profile(&h);

    printf("height %d   profile %d   flags 0x%02x   nbits %08x\n",
           h.height, profile, h.flags, h.nbits);

    len = ic_knots_payload(&h, payload, sizeof(payload));
    if (len < 0) {
        fprintf(stderr, "payload: %s\n", ic_strerror(len));
        return 1;
    }
    printf("asic buffer %d bytes, the chip varies 0x%02X..0x%02X\n",
           len, (unsigned)ic_knots_roll_offset(profile),
           (unsigned)ic_knots_roll_offset(profile) + 15);

    ic_knots_xor_mask(h.xor_key, h.xor_key_clear_bits, mask);
    ic_knots_pow_hash(payload, (size_t)len, mask, pow);

    printf("computed ");
    for (i = 0; i < 32; i++) printf("%02x", pow[i]);
    printf("\nchain    ");
    for (i = 0; i < 32; i++) printf("%02x", want[i]);
    printf("\n");

    if (ic_knots_target_be(h.nbits, target) == IC_OK) {
        bound = ic_knots_bound_from_target(target);
        printf("bound the chip would be given: %016llx%s\n",
               (unsigned long long)bound,
               ic_knots_asic_target_exact(&h) ? ""
                   : "  (masked: the chip cannot see the real target)");
        printf("meets target: %s\n",
               ic_knots_meets_target(pow, target) ? "yes" : "NO");
    }

    /* Which parts could have mined it. */
    printf("parts that can hash this profile:");
    {
        const struct ic_family *fam[3] = { &ic_ict580, &ic_icc590, &ic_ica586 };
        int any = 0;

        for (i = 0; i < 3; i++)
            if (ic_knots_fits(fam[i], profile)) {
                printf(" %s", fam[i]->name);
                any = 1;
            }
        printf("%s\n", any ? "" : " none in this family");
    }

    if (memcmp(pow, want, 32) != 0) {
        printf("\nMISMATCH\n");
        return 1;
    }
    printf("\nmatch\n");
    return 0;
}
