/* ic-probe - first contact with a chain.
 *
 * Read-only by default: bring the SPI port up, enumerate, then read the good
 * core count and the temperature off every chip. Nothing here starts the chain
 * hashing or touches the PLL unless you ask for it.
 *
 *   ic-probe -d /dev/spidev1.0 -p ICA586
 *   ic-probe -d /dev/spidev1.0 -p ICC590 -v --selftest
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intchains.h"

static int verbose;
static struct ic_spi spi;
static struct ic_transport under;

static void hexdump(const char *tag, const uint8_t *b, size_t len)
{
    size_t i, shown = len > 48 ? 48 : len;

    printf("  %s [%zu]", tag, len);
    for (i = 0; i < shown; i++)
        printf("%s%02X", (i % 16) ? " " : "\n    ", b[i]);
    if (shown < len)
        printf("  ...");
    printf("\n");
}

static int traced_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    int rc;

    if (verbose)
        hexdump("MOSI", tx, len);
    rc = under.xfer(ctx, tx, rx, len);
    if (verbose)
        hexdump("MISO", rx, len);
    return rc;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: ic-probe -d <spidev> -p <ICT580|ICC590|ICA586> [-v] [--selftest]\n"
        "                [--freq <MHz>]\n"
        "\n"
        "  -d   spidev node, one per control board\n"
        "  -p   part number; configuration model strings are not reliable\n"
        "  -v   hexdump every transfer\n"
        "  --selftest  run the broadcast self-test triple\n"
        "  --freq      set the chain clock, which is a write\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *dev = NULL, *part = NULL;
    const struct ic_family *fam;
    struct ic_chain ch;
    struct ic_transport tr;
    double freq = 0.0;
    int selftest = 0, nchips = 0, i, rc, cores = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d") && i + 1 < argc)      dev = argv[++i];
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) part = argv[++i];
        else if (!strcmp(argv[i], "-v"))                 verbose = 1;
        else if (!strcmp(argv[i], "--selftest"))         selftest = 1;
        else if (!strcmp(argv[i], "--freq") && i + 1 < argc) freq = atof(argv[++i]);
        else usage();
    }
    if (!dev || !part)
        usage();

    fam = ic_family_by_name(part);
    if (!fam) {
        fprintf(stderr, "unknown part %s\n", part);
        return 2;
    }

    rc = ic_spi_open(&spi, dev, fam->spi_hz, fam->spi_mode);
    if (rc != IC_OK) {
        fprintf(stderr, "%s: %s\n", dev, ic_strerror(rc));
        return 1;
    }
    under = ic_spi_transport(&spi);
    tr.xfer = traced_xfer;
    tr.ctx  = under.ctx;
    ic_chain_init(&ch, fam, tr);

    printf("%s on %s at %u Hz mode %u\n", fam->name, dev, fam->spi_hz, fam->spi_mode);

    rc = ic_enumerate(&ch, &nchips);
    if (rc != IC_OK) {
        fprintf(stderr, "enumerate: %s\n", ic_strerror(rc));
        fprintf(stderr, "the chain is unpowered, held in reset, or on another node\n");
        ic_spi_close(&spi);
        return 1;
    }
    printf("chips: %d\n", nchips);

    if (selftest) {
        rc = ic_selftest(&ch);
        printf("selftest: %s\n", rc == IC_OK ? "pass" : ic_strerror(rc));
    }

    if (freq > 0.0) {
        double actual = 0.0;

        rc = ic_set_freq(&ch, IC_BROADCAST, freq, &actual);
        if (rc != IC_OK)
            fprintf(stderr, "set %g MHz: %s\n", freq, ic_strerror(rc));
        else
            printf("clock: %g MHz requested, %g MHz actual\n", freq, actual);
    }

    printf("\n chip  cores  temp\n");
    for (i = 1; i <= nchips; i++) {
        uint32_t good = 0, sensor = 0;
        int temp;

        rc = ic_read_reg(&ch, (uint8_t)i, IC_REG_GOOD_CORES, &good);
        if (rc != IC_OK) {
            printf(" %4d  %s\n", i, ic_strerror(rc));
            continue;
        }
        cores += (int)(good & 0xFF);

        rc = ic_read_reg(&ch, (uint8_t)i, IC_REG_SENSOR_DATA, &sensor);
        temp = (rc == IC_OK) ? ic_temp_from_raw((uint16_t)(sensor & 0xFFF))
                             : IC_TEMP_INVALID;
        printf(" %4d  %5u  %4d C%s\n", i, good & 0xFF, temp,
               temp == IC_TEMP_INVALID ? "  (out of range)" : "");
    }

    printf("\ngood cores: %d of %d\n", cores,
           fam->cores_per_chip ? nchips * fam->cores_per_chip : 0);
    printf("temperatures are approximate: the controller's 156-entry table is "
           "not in the datasheet\n");

    ic_spi_close(&spi);
    return 0;
}
