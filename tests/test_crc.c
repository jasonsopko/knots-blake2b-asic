#include "intchains.h"
#include "ic_test.h"

/* Recompute the polynomial bitwise so a corrupted table cannot pass. */
static uint16_t bitwise(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0;
    size_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= buf[i];
        for (b = 0; b < 8; b++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
    }
    return crc;
}

static void check_vector(void)
{
    /* The published CRC-16/KERMIT check value. */
    const uint8_t s[] = "123456789";

    CHECK_EQ(ic_crc16_kermit(s, 9), 0x2189);
    CHECK_EQ(bitwise(s, 9), 0x2189);
}

static void table_matches_polynomial(void)
{
    uint8_t b[1];
    int i;

    for (i = 0; i < 256; i++) {
        b[0] = (uint8_t)i;
        CHECK_EQ(ic_crc16_kermit(b, 1), bitwise(b, 1));
    }
}

static void wire_swaps_packets(void)
{
    /* The wire form runs over the packets serialized little-endian, so it must
     * equal the plain form over a byte-swapped copy. */
    const uint8_t wire[] = { 0xA5, 0x3C, 0x0A, 0x00, 0x00, 0x07 };
    uint8_t swapped[6];
    size_t i;

    for (i = 0; i < sizeof(wire); i++)
        swapped[i] = wire[i ^ 1];

    CHECK_EQ(ic_crc16_wire(wire, sizeof(wire)), ic_crc16_kermit(swapped, sizeof(swapped)));
}

static void odd_length_is_a_hard_error(void)
{
    const uint8_t b[3] = { 1, 2, 3 };

    CHECK_EQ(ic_crc16_wire(b, 3), 0xFFFF);
}

TEST_MAIN("crc",
    check_vector();
    table_matches_polynomial();
    wire_swaps_packets();
    odd_length_is_a_hard_error();
)
