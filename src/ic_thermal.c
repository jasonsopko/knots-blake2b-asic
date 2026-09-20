/* ic_thermal.c - register 7 to degrees, section 12.
 *
 * The controller carries a 156-entry monotonic lookup table. That table is not
 * in the datasheet, so until someone lifts it out of a firmware image this
 * interpolates the two documented anchors: raw 2804 is 125 C, raw 3420 is
 * -30 C, about four counts per degree. Expect a degree or two of error against
 * the real table, and more outside the anchors.
 */
#include "intchains.h"

#define ANCHOR_HOT_RAW   2804
#define ANCHOR_HOT_C     125
#define ANCHOR_COLD_RAW  3420
#define ANCHOR_COLD_C    (-30)

static const uint16_t *tab_raw;
static const int8_t   *tab_c;
static size_t          tab_n;

void ic_temp_set_table(const uint16_t *thresholds, const int8_t *temps, size_t n)
{
    tab_raw = thresholds;
    tab_c   = temps;
    tab_n   = (thresholds && temps) ? n : 0;
}

int ic_temp_from_raw(uint16_t raw)
{
    if (tab_n) {
        size_t i;

        /* The lookup takes the first entry whose threshold is at or above the
         * raw code; past the end of the table the reading is out of range. */
        for (i = 0; i < tab_n; i++)
            if (raw <= tab_raw[i])
                return tab_c[i];
        return IC_TEMP_INVALID;
    }

    /* A lower raw code is a hotter chip. The real table's first entry wins for
     * anything at or below its threshold, so the hot end clamps rather than
     * going out of range; only running off the cold end is a bad reading.
     * Getting this backwards would report an overheating chip as -35, which a
     * thermal loop would read as cold. */
    if (raw <= ANCHOR_HOT_RAW)
        return ANCHOR_HOT_C;
    if (raw > ANCHOR_COLD_RAW)
        return IC_TEMP_INVALID;

    /* Linear between the anchors, rounded to the nearest degree. The slope is
     * negative: a larger raw code is a colder chip. */
    {
        long span_raw = ANCHOR_COLD_RAW - ANCHOR_HOT_RAW;          /* 616 */
        long span_c   = (long)ANCHOR_HOT_C - ANCHOR_COLD_C;        /* 155 */
        long off      = (long)raw - ANCHOR_HOT_RAW;
        return (int)(ANCHOR_HOT_C - (off * span_c * 2 + span_raw) / (span_raw * 2));
    }
}
