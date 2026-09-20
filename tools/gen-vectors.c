/* gen-vectors - golden vectors for the profile 1 RTL.
 *
 * Writes tb/core_vectors.mem and tb/miner_vector.mem from the same library the
 * host controller uses, so the simulation and the controller cannot drift
 * apart. Checks itself against the Knots header vector before writing
 * anything; if that fails, nothing downstream is worth running.
 */
#include <stdio.h>
#include <string.h>

#include "intchains_knots.h"
#include "knots_profile1_vector.h"

#define NVEC 8
#define WORDS_PER_VEC 20            /* m[0..15] then the four digest words */

static void hexline(FILE *f, uint64_t v)
{
    fprintf(f, "%016llx\n", (unsigned long long)v);
}

static uint64_t digest_word(const uint8_t d[32], int i)
{
    uint64_t w = 0;
    int j;

    for (j = 7; j >= 0; j--)
        w = w << 8 | d[i * 8 + j];
    return w;
}

/* A profile 1 header with everything varied by k, so the vectors exercise the
 * whole path from header fields down rather than a hand-made buffer. */
static void make_header(struct ic_knots_header *h, int k)
{
    int i;

    memset(h, 0, sizeof(*h));
    h->version = 0x20000000;
    h->flags   = 1;                 /* profile 1 */
    h->height  = 840000 + k;
    h->ntime   = 2000000000u + (uint32_t)k;
    h->nbits   = 0x1d00FFFFu;
    h->txcount = (uint16_t)(3 + k);
    h->nonce       = 0x11111111u * (uint32_t)k;
    h->nonce2      = 0x22222222u * (uint32_t)k;
    h->nonce3      = 0x33333333u * (uint32_t)k;
    h->time_offset = (uint32_t)(600 + k);

    for (i = 0; i < 32; i++) {
        h->prev_block[i]  = (uint8_t)(i + k);
        h->merkle_root[i] = (uint8_t)(i * 3 + k);
        h->mm_rhs[i]      = (uint8_t)(i * 7 + k);
    }
    for (i = 0; i < 16; i++)
        h->extranonce[i] = (uint8_t)(i * 11 + k);
}

int main(void)
{
    uint8_t buf[160], dig[32];
    uint64_t m[16];
    struct ic_knots_header h;
    FILE *f;
    int i, k;

    /* ---- agree with the node before generating anything ----------------- */
    ic_knots_digest(knots_asic_input, 80, dig);
    if (memcmp(dig, knots_expected_digest, 32) != 0) {
        fprintf(stderr, "model disagrees with %s, refusing to generate\n",
                knots_vector_name);
        return 1;
    }
    printf("model agrees with %s\n", knots_vector_name);

    /* ---- core vectors --------------------------------------------------- */
    f = fopen("tb/core_vectors.mem", "w");
    if (!f) { perror("tb/core_vectors.mem"); return 1; }
    fprintf(f, "// profile 1 BLAKE2b vectors: per vector, m[0..15] then the\n"
               "// four digest words. Vector 0 is %s.\n", knots_vector_name);

    for (k = 0; k < NVEC; k++) {
        size_t len = 80;

        if (k == 0) {
            memcpy(buf, knots_asic_input, 80);
        } else {
            make_header(&h, k);
            if (ic_knots_payload(&h, buf, sizeof(buf)) != 80) {
                fprintf(stderr, "payload %d is not 80 bytes\n", k);
                return 1;
            }
        }
        ic_knots_digest(buf, len, dig);
        if (ic_knots_message_words(buf, len, m) != IC_OK)
            return 1;

        fprintf(f, "// vector %d  compare value %016llx\n",
                k, (unsigned long long)ic_knots_compare_value(dig));
        for (i = 0; i < 16; i++)
            hexline(f, m[i]);
        for (i = 0; i < 4; i++)
            hexline(f, digest_word(dig, i));
    }
    fclose(f);
    printf("tb/core_vectors.mem: %d vectors\n", NVEC);

    /* ---- miner vector: a real search, short enough to simulate ----------- */
    {
        const uint64_t bound = UINT64_C(0x00FFFFFFFFFFFFFF);   /* about 1 in 256 */
        uint64_t n, found = 0;
        int hit = 0;

        make_header(&h, 1);

        /* The chip rolls the eight bytes at the roll offset as one counter.
         * On profile 1 those are nNonce and m_nonce2. */
        for (n = 0; n < 100000; n++) {
            h.nonce  = (uint32_t)n;
            h.nonce2 = (uint32_t)(n >> 32);
            if (ic_knots_payload(&h, buf, sizeof(buf)) != 80)
                return 1;
            ic_knots_digest(buf, 80, dig);
            if (ic_knots_compare_value(dig) <= bound) {
                found = n;
                hit = 1;
                break;
            }
        }
        if (!hit) {
            fprintf(stderr, "no hit found, widen the bound\n");
            return 1;
        }

        h.nonce  = (uint32_t)found;
        h.nonce2 = (uint32_t)(found >> 32);
        ic_knots_payload(&h, buf, sizeof(buf));
        ic_knots_digest(buf, 80, dig);
        ic_knots_message_words(buf, 80, m);

        f = fopen("tb/miner_vector.mem", "w");
        if (!f) { perror("tb/miner_vector.mem"); return 1; }
        fprintf(f, "// m[1..9], then bound, nonce_start, expected nonce.\n");
        for (i = 1; i <= 9; i++)
            hexline(f, m[i]);
        hexline(f, bound);
        hexline(f, 0);
        hexline(f, found);
        fclose(f);

        printf("tb/miner_vector.mem: hit at nonce %llu after %llu tries,"
               " compare value %016llx\n",
               (unsigned long long)found, (unsigned long long)(found + 1),
               (unsigned long long)ic_knots_compare_value(dig));
    }

    return 0;
}
