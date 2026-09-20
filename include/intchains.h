/* intchains.h - host interface to the Intchains Blake2B mining ASICs.
 *
 * Implements the rev 0.2 host interface specification, which was reverse
 * engineered and written by Luke Dashjr and published at
 * https://luke.dashjr.org/tmp/code/intchains-asic-datasheet.html
 *
 * docs/datasheet.md is a transcription of it and the section numbers in the
 * comments below refer to that file. The specification was established by
 * static analysis and has not been confirmed against hardware; anything this
 * header calls an assumption is ours, not the specification's, and is listed
 * in docs/open-questions.md.
 */
#ifndef INTCHAINS_H
#define INTCHAINS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ errors */

enum {
    IC_OK          =  0,
    IC_ERR_ARG     = -1,   /* caller passed something impossible */
    IC_ERR_IO      = -2,   /* the transport failed */
    IC_ERR_NOREPLY = -3,   /* no frame in the receive buffer matched */
    IC_ERR_CRC     = -4,   /* a frame matched but its checksum did not */
    IC_ERR_RANGE   = -5,   /* value outside what the device accepts */
    IC_ERR_STATE   = -6    /* chain not enumerated, or a step out of order */
};

const char *ic_strerror(int err);

/* ------------------------------------------------- section 4, frame format */

#define IC_PREAMBLE        0xA53Cu
#define IC_TAIL            0x0000u
#define IC_BROADCAST       0x00u
#define IC_MAX_PAYLOAD_PKT 72                       /* ICC590 work payload */
#define IC_MAX_FRAME_PKT   (IC_MAX_PAYLOAD_PKT + 4) /* preamble cmd crc tail */
#define IC_MAX_FRAME_BYTES (IC_MAX_FRAME_PKT * 2)

/* Broadcast turnaround is fixed at 900 bytes (section 6). */
#define IC_BROADCAST_FILLER 900
#define IC_FILLER_PER_HOP   6

/* The address is a full byte whatever the chain length, so the worst
 * turnaround is not the broadcast one: chip 255 is 1530 filler bytes away.
 * Size for that, or addressed commands past chip 198 fail with IC_ERR_ARG. */
#define IC_MAX_ADDR_FILLER  (IC_FILLER_PER_HOP * 255)
#define IC_MAX_FILLER       (IC_BROADCAST_FILLER > IC_MAX_ADDR_FILLER \
                             ? IC_BROADCAST_FILLER : IC_MAX_ADDR_FILLER)
#define IC_MAX_XFER_BYTES   (IC_MAX_FRAME_BYTES + IC_MAX_FILLER + IC_MAX_FRAME_BYTES)

/* Command word 0xYONN: Y tag [15:12], O opcode [11:8], NN chip address [7:0]. */
#define IC_CMD(tag, op, addr) \
    ((uint16_t)((((tag) & 0xF) << 12) | (((op) & 0xF) << 8) | ((addr) & 0xFF)))
#define IC_CMD_TAG(c)  ((uint8_t)(((c) >> 12) & 0xF))
#define IC_CMD_OP(c)   ((uint8_t)(((c) >> 8) & 0xF))
#define IC_CMD_ADDR(c) ((uint8_t)((c) & 0xFF))

enum ic_opcode {
    IC_OP_SELFTEST_1 = 0x1,   /* 01NN */
    IC_OP_AUTOADDR   = 0x2,   /* 0200 */
    IC_OP_SELFTEST_3 = 0x3,   /* 03NN */
    IC_OP_WORKCFG    = 0x4,   /* 04NN */
    IC_OP_LOADWORK   = 0x7,   /* Y7NN */
    IC_OP_POLL       = 0x8,   /* 0800 */
    IC_OP_WRITEREG   = 0x9,   /* 09NN */
    IC_OP_READREG    = 0xA,   /* 0ANN */
    IC_OP_SELFTEST_2 = 0xB,   /* 0BNN */
    IC_OP_VENDOR_C   = 0xC    /* 0C00, purpose unknown, section 13.3 */
};

/* The controller matches replies against wildcard templates in which N, X and
 * Y are don't-care nibbles (section 4.1). A zero mask bit is a don't-care. */
struct ic_tmpl {
    uint16_t value;
    uint16_t mask;
};

static inline int ic_tmpl_match(struct ic_tmpl t, uint16_t cmd)
{
    return (cmd & t.mask) == (t.value & t.mask);
}

/* --------------------------------------------------- section 5, checksum */

/* Plain CRC-16/KERMIT over a byte stream. check("123456789") == 0x2189. */
uint16_t ic_crc16_kermit(const uint8_t *buf, size_t len);

/* The on-wire variant: the bytes of each 16-bit packet are swapped before the
 * CRC runs. Returns 0xFFFF on an odd length, as the controller does. */
uint16_t ic_crc16_wire(const uint8_t *wire, size_t len);

/* ----------------------------------------------- section 4, frame assembly */

/* Serialize preamble, cmd, payload, optional CRC and tail into out. Payload
 * packets are host-order uint16 and go out most-significant byte first.
 * Returns the byte length written, or IC_ERR_ARG. */
int ic_frame_build(uint8_t *out, size_t outsz, uint16_t cmd,
                   const uint16_t *payload, size_t npkt, int with_crc);

struct ic_reply {
    size_t   offset;                       /* where the preamble was found */
    uint16_t cmd;
    uint16_t pkt[IC_MAX_PAYLOAD_PKT];
    size_t   npkt;
    int      crc_ok;                       /* 1, or 0 when no CRC was expected */
};

/* Scan rx for a preamble followed by a command word matching one of the
 * templates, then lift npkt payload packets and check the CRC if expected.
 * Every preamble occurrence is tried, because the reply is not at a fixed
 * offset (section 4). Returns IC_OK, IC_ERR_NOREPLY or IC_ERR_CRC. */
int ic_frame_find(const uint8_t *rx, size_t rxlen,
                  const struct ic_tmpl *tmpl, size_t ntmpl,
                  size_t npkt, int has_crc, struct ic_reply *out);

/* --------------------------------------------------- section 6, bus timing */

/* Filler bytes of 0x00 to append after the request so the reply has time to
 * walk back down the chain. Lengths are whole frames, in bytes. */
size_t ic_filler_bytes(uint8_t addr, size_t frame_len, size_t reply_len);

/* Byte length of a frame carrying npkt payload packets. */
static inline size_t ic_frame_len(size_t npkt, int with_crc)
{
    return (npkt + 3u + (with_crc ? 1u : 0u)) * 2u;   /* preamble cmd .. tail */
}

/* -------------------------------------------------- section 2, device family */

/* Per-part quirks, so behaviour follows the part rather than the address of
 * the struct describing it. A caller may hold their own copy. */
#define IC_QUIRK_TARGET_TAG_13DF 0x1u   /* 0x13,0xDF over the target's top two
                                         * bytes after it is written (9, ICA586) */

struct ic_family {
    const char *name;
    int         cores_per_chip;   /* build constant, 0 when unknown (ICC551) */
    size_t      work_pkt;         /* work payload, 16-bit packets */
    size_t      work_bytes;       /* work payload, bytes */
    size_t      header_bytes;     /* header the payload carries, 80 or 128 */
    uint64_t    nonce_ceiling;
    uint32_t    spi_hz;
    uint8_t     spi_mode;
    uint32_t    pll_mask;
    uint32_t    quirks;
};

extern const struct ic_family ic_ict580;   /* SCBox, SCBox II, SC6-SE */
extern const struct ic_family ic_icc590;   /* HS3, HS5, HS6, HS-BOX, ... */
extern const struct ic_family ic_ica586;   /* SC5 Pro II */
extern const struct ic_family ic_icc551;   /* compiled in, never bound */

/* Configuration model strings are documentation, not selection (section 2
 * footnote b). Resolve by part number only, and never trust product.json. */
const struct ic_family *ic_family_by_name(const char *name);

/* ------------------------------------------------------------- transport */

/* One full-duplex transfer of len bytes. Returns 0 on success. */
struct ic_transport {
    int (*xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
    void *ctx;
};

/* ------------------------------------------------------------- chain handle */

struct ic_chain {
    struct ic_transport     tr;
    const struct ic_family *fam;
    int                     nchips;      /* 0 until ic_enumerate succeeds */
    int                     good_cores;  /* sum over the chain, after bring-up */
    uint8_t                 tx[IC_MAX_XFER_BYTES];
    uint8_t                 rx[IC_MAX_XFER_BYTES];
};

void ic_chain_init(struct ic_chain *ch, const struct ic_family *fam,
                   struct ic_transport tr);

/* ------------------------------------------------ section 7, command layer */

/* A command can have more than one shape of reply: a poll comes back as either
 * a bare echo or a five packet result, so the packet count and the presence of
 * a CRC belong to the template, not to the command. */
struct ic_reply_form {
    struct ic_tmpl tmpl;
    int            npkt;         /* -1: the family's work packet count */
    int            crc;
};

struct ic_cmd_desc {
    uint8_t              op;
    const char          *name;
    int                  tx_npkt;     /* -1: the family's work packet count */
    int                  tx_crc;
    int                  retries;
    int                  reply_zeros; /* reply is an all-zero frame (7.4) */
    struct ic_reply_form reply[2];
    int                  nreply;
};

const struct ic_cmd_desc *ic_cmd_desc(uint8_t op);

/* Run one command to completion, retries included. reply may be NULL. */
int ic_xact(struct ic_chain *ch, uint16_t cmd,
            const uint16_t *payload, size_t npkt, struct ic_reply *reply);

/* ------------------------------------------------------------- operations */

int ic_enumerate(struct ic_chain *ch, int *nchips);      /* section 7.2 */
int ic_selftest(struct ic_chain *ch);                    /* section 7.1 */
int ic_read_reg(struct ic_chain *ch, uint8_t addr, uint16_t reg, uint32_t *val);
int ic_write_reg(struct ic_chain *ch, uint8_t addr, uint16_t reg, uint32_t val);

/* ------------------------------------------------- section 8, register map */

enum ic_reg {
    IC_REG_PLL         = 0,
    IC_REG_GOOD_CORES  = 3,
    IC_REG_CHAIN_START = 4,
    IC_REG_SENSOR_MODE = 6,
    IC_REG_SENSOR_DATA = 7,
    IC_REG_IDENTIFY    = 0xFFFF
};

/* Register 4, low 16 bits, identical on every product examined. */
#define IC_CHAIN_START_LOW   0x03F1u
#define IC_CHAIN_START_NIB_7 0x70000000u   /* chains of 84 and 96 chips */
#define IC_CHAIN_START_NIB_8 0x80000000u   /* chains of 16, 36 and 46 chips */

/* The top nibble of register 4 is unexplained (section 13.1). This returns the
 * value the index-budget hypothesis predicts, which reproduces all twelve
 * observed products. It is a hypothesis; prefer a value read off the product
 * you are replacing when you have one. */
uint32_t ic_chain_start_value(int nchips);

int ic_start_hashing(struct ic_chain *ch, uint32_t reg4);  /* section 14 step 13 */

/* -------------------------------------------------- section 11, PLL / clock */

#define IC_PLL_REF_MHZ   25.0
#define IC_PLL_NF_MIN    0x15    /* nf must be > 0x14 */
#define IC_PLL_NF_MAX    0x77

struct ic_pll {
    int    div, nr, od;
    int    nf;
    double actual_mhz;
};

int      ic_pll_solve(double mhz, struct ic_pll *out);
uint32_t ic_pll_reg0(uint32_t current, const struct ic_pll *p, uint32_t mask);
int      ic_set_freq(struct ic_chain *ch, uint8_t addr, double mhz, double *actual);

/* ------------------------------------------------------ section 12, thermal */

#define IC_TEMP_INVALID (-35)    /* the out-of-range sentinel */

/* Register 7 raw code to degrees C. Without the controller's 156-entry table
 * this interpolates the two documented anchors; see docs/open-questions.md. */
int  ic_temp_from_raw(uint16_t raw);

/* Install the real table once it has been lifted out of a firmware image.
 * thresholds must be ascending, temps the matching degrees. */
void ic_temp_set_table(const uint16_t *thresholds, const int8_t *temps, size_t n);

/* Sensor period field of register 7, from a millisecond interval. */
static inline uint16_t ic_sensor_period(unsigned ms)
{
    return (uint16_t)((unsigned long)ms * 1000000ul / 45000ul);
}

/* ------------------------------------------------- section 9, work payload */

struct ic_work {
    const uint8_t *header;       /* 80 B, or 128 B on ICC590 */
    size_t         header_len;
    uint64_t       nonce_start;  /* written at offset 0x20 */
    uint64_t       timestamp;    /* written at offset 0x28 */
    uint8_t        target[32];   /* 256-bit target, as the firmware holds it */
};

/* Build the family's work payload. Returns the byte length, or IC_ERR_ARG. */
int ic_work_build(const struct ic_family *fam, const struct ic_work *w,
                  uint8_t *out, size_t outsz);

/* Broadcast 04NN then Y7NN (section 7.4). work_id is the 4-bit tag. */
#define IC_WORKCFG_OPERAND 0x00E5u     /* compile-time constant, section 13.2 */
int ic_load_work(struct ic_chain *ch, uint8_t work_id,
                 const uint8_t *payload, size_t len);

/* ------------------------------------------------ section 10, result report */

struct ic_result {
    uint8_t  work_id;
    uint8_t  chip;
    uint64_t nonce;
    uint8_t  ts_index;
    uint8_t  core;
};

void ic_result_decode(uint16_t cmd, const uint16_t *pkt, struct ic_result *out);

/* The 8 bytes this nonce becomes at header offset 32. */
void ic_result_nonce_bytes(uint64_t nonce, uint8_t out[8]);

/* Returns 1 and fills res when the chain had a result, 0 when it was empty,
 * or a negative error (section 7.5). */
int ic_poll(struct ic_chain *ch, struct ic_result *res);

/* ---------------------------------------------- section 14, initialization */

/* Board-level signals (section 15). The mining path never drives the ISL8118
 * rail, so there is no voltage hook here; see section 13.4. */
struct ic_board {
    int (*set_reset)(void *ctx, int level);
    int (*set_enable)(void *ctx, int level);
    int (*set_plugin)(void *ctx, int level);
    void (*delay_ms)(void *ctx, unsigned ms);
    void *ctx;
};

#define IC_BRINGUP_ATTEMPTS 5
#define IC_BRINGUP_MHZ_LOW  50.0
#define IC_BRINGUP_MHZ_HIGH 500.0

/* Steps 1 to 13 of section 14, one attempt. Use ic_bringup for the retries. */
int ic_bringup_once(struct ic_chain *ch, const struct ic_board *b);
int ic_bringup(struct ic_chain *ch, const struct ic_board *b);

/* ----------------------------------------------------------- spidev transport */

struct ic_spi {
    int fd;
};

int  ic_spi_open(struct ic_spi *s, const char *path, uint32_t hz, uint8_t mode);
void ic_spi_close(struct ic_spi *s);
struct ic_transport ic_spi_transport(struct ic_spi *s);

#ifdef __cplusplus
}
#endif
#endif /* INTCHAINS_H */
