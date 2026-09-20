/* ic_pll.c - clock configuration, section 11. */
#include "intchains.h"

/* Identical table in all four back-ends. The terminator is a real entry in the
 * listing and the loop can fall off onto it; we return IC_ERR_RANGE there
 * rather than divide by a zero divider. */
static const struct { int div, nr, od; } tbl[] = {
    { 48, 6, 2 }, { 32, 4, 2 }, { 24, 6, 1 }, { 16, 4, 1 }, { 8, 2, 1 },
    { 6, 6, 0 },  { 2, 2, 0 },  { 1, 1, 0 },  { 0, 0, 0 }
};

int ic_pll_solve(double mhz, struct ic_pll *out)
{
    int i, nf = 0;

    if (!out || mhz <= 0.0)
        return IC_ERR_ARG;

    /* Find a post-divider that puts the feedback divider inside its window. */
    for (i = 0; tbl[i].div; i++) {
        nf = (int)(tbl[i].div * mhz / IC_PLL_REF_MHZ);
        if (nf > 0x14 && nf <= 0x77)
            break;
    }
    if (!tbl[i].div)
        return IC_ERR_RANGE;

    out->div        = tbl[i].div;
    out->nr         = tbl[i].nr;
    out->od         = tbl[i].od;
    out->nf         = nf;
    out->actual_mhz = nf * IC_PLL_REF_MHZ / tbl[i].div;
    return IC_OK;
}

uint32_t ic_pll_reg0(uint32_t current, const struct ic_pll *p, uint32_t mask)
{
    return (current & mask)
         | (uint32_t)(p->nf << 16)
         | (uint32_t)(p->od << 8)
         | (uint32_t)(p->nr << 4);
}

int ic_set_freq(struct ic_chain *ch, uint8_t addr, double mhz, double *actual)
{
    struct ic_pll p;
    uint32_t r0;
    int rc;

    if (!ch || !ch->fam)
        return IC_ERR_ARG;

    rc = ic_pll_solve(mhz, &p);
    if (rc != IC_OK)
        return rc;

    /* Read-modify-write under the family's mask, then clear bit 7 to release
     * bypass and engage the new clock. The read at a broadcast address is what
     * the controller does; whichever chip answers supplies the bits outside
     * the mask. See docs/open-questions.md. */
    rc = ic_read_reg(ch, addr, IC_REG_PLL, &r0);
    if (rc != IC_OK)
        return rc;

    rc = ic_write_reg(ch, addr, IC_REG_PLL, ic_pll_reg0(r0, &p, ch->fam->pll_mask));
    if (rc != IC_OK)
        return rc;

    rc = ic_read_reg(ch, addr, IC_REG_PLL, &r0);
    if (rc != IC_OK)
        return rc;

    rc = ic_write_reg(ch, addr, IC_REG_PLL, r0 & ~0x80u);
    if (rc != IC_OK)
        return rc;

    if (actual)
        *actual = p.actual_mhz;
    return IC_OK;
}
