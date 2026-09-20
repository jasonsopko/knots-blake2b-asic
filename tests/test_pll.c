#include "intchains.h"
#include "ic_test.h"

static void bringup_frequencies(void)
{
    struct ic_pll p;

    /* 50 MHz, the frequency bring-up uses before self-test. */
    CHECK_EQ(ic_pll_solve(50.0, &p), IC_OK);
    CHECK_EQ(p.div, 48);
    CHECK_EQ(p.nr, 6);
    CHECK_EQ(p.od, 2);
    CHECK_EQ(p.nf, 96);
    CHECK(p.actual_mhz == 50.0);

    /* 500 MHz, the ramp after the chain has enumerated. */
    CHECK_EQ(ic_pll_solve(500.0, &p), IC_OK);
    CHECK_EQ(p.div, 2);
    CHECK_EQ(p.nr, 2);
    CHECK_EQ(p.od, 0);
    CHECK_EQ(p.nf, 40);
    CHECK(p.actual_mhz == 500.0);
}

static void feedback_divider_stays_in_its_window(void)
{
    struct ic_pll p;
    double mhz;

    for (mhz = 11.0; mhz <= 1400.0; mhz += 0.5) {
        if (ic_pll_solve(mhz, &p) != IC_OK)
            continue;
        CHECK(p.nf > 0x14 && p.nf <= 0x77);
        CHECK(p.div > 0);
    }
}

static void unreachable_frequencies_are_an_error(void)
{
    struct ic_pll p;

    /* Below about 10.94 MHz no divider puts nf inside the window, and the
     * listing's loop falls onto the zero terminator. */
    CHECK_EQ(ic_pll_solve(5.0, &p), IC_ERR_RANGE);
    CHECK_EQ(ic_pll_solve(0.0, &p), IC_ERR_ARG);
    CHECK_EQ(ic_pll_solve(1.0e6, &p), IC_ERR_RANGE);
}

static void register_zero_composition(void)
{
    struct ic_pll p;
    uint32_t r0;

    CHECK_EQ(ic_pll_solve(500.0, &p), IC_OK);

    /* Everything outside the mask is replaced; everything inside survives. */
    r0 = ic_pll_reg0(0xFFFFFFFFu, &p, 0xFE00FC8Fu);
    CHECK_EQ((r0 >> 16) & 0x1FF, 40);        /* NF  [24:16] */
    CHECK_EQ((r0 >> 8) & 0x3, 0);            /* OD  [9:8]   */
    CHECK_EQ((r0 >> 4) & 0x7, 2);            /* NR  [6:4]   */
    CHECK_EQ(r0 & 0xFE00FC8Fu, 0xFE00FC8Fu); /* masked bits kept */
}

/* The interpolating fallback and a real table have to agree on the edges,
 * especially the hot one: a lower raw code is a hotter chip, so returning the
 * out-of-range sentinel there would read as cold to a thermal loop. */
static void thermal_edges_agree_with_a_table(void)
{
    static const uint16_t th[3] = { 2804, 3112, 3420 };
    static const int8_t   tc[3] = { 125, 47, -30 };
    uint16_t hot[4] = { 1500, 2000, 2700, 2804 };
    int i, fallback[4];

    for (i = 0; i < 4; i++) {
        fallback[i] = ic_temp_from_raw(hot[i]);
        CHECK_EQ(fallback[i], 125);                 /* clamped, not sentinel */
        CHECK(fallback[i] != IC_TEMP_INVALID);
    }
    CHECK_EQ(ic_temp_from_raw(3420), -30);
    CHECK_EQ(ic_temp_from_raw(3421), IC_TEMP_INVALID);
    CHECK_EQ(ic_temp_from_raw(4000), IC_TEMP_INVALID);

    ic_temp_set_table(th, tc, 3);
    for (i = 0; i < 4; i++)
        CHECK_EQ(ic_temp_from_raw(hot[i]), fallback[i]);
    CHECK_EQ(ic_temp_from_raw(3421), IC_TEMP_INVALID);
    ic_temp_set_table(NULL, NULL, 0);
}

static void sensor_period_field(void)
{
    /* Bring-up passes 25 ms, which the controller turns into 555. */
    CHECK_EQ(ic_sensor_period(25), 555);
}

TEST_MAIN("pll",
    bringup_frequencies();
    feedback_divider_stays_in_its_window();
    unreachable_frequencies_are_an_error();
    register_zero_composition();
    thermal_edges_agree_with_a_table();
    sensor_period_field();
)
