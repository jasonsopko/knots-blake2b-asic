/* ic_spi.c - spidev transport, section 3.
 *
 * One command is one SPI_IOC_MESSAGE(1): the request, the turnaround and the
 * reply all ride in a single full-duplex transfer. There is no separate read
 * and no interrupt or ready line.
 */
#include "intchains.h"

#if defined(__linux__)

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int ic_spi_open(struct ic_spi *s, const char *path, uint32_t hz, uint8_t mode)
{
    uint8_t bits = 8;

    if (!s || !path)
        return IC_ERR_ARG;

    s->fd = open(path, O_RDWR);                  /* one device per CPB */
    if (s->fd < 0)
        return IC_ERR_IO;

    if (ioctl(s->fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0 ||
        ioctl(s->fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(s->fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        close(s->fd);
        s->fd = -1;
        return IC_ERR_IO;
    }
    /* Bit order is left alone: MSB first is the spidev default and the
     * controller never overrides it. */
    return IC_OK;
}

void ic_spi_close(struct ic_spi *s)
{
    if (s && s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
}

static int spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    struct ic_spi *s = ctx;
    struct spi_ioc_transfer t;

    if (!s || s->fd < 0 || len == 0)
        return -1;

    memset(&t, 0, sizeof(t));
    t.tx_buf = (unsigned long)tx;
    t.rx_buf = (unsigned long)rx;
    t.len    = (uint32_t)len;

    return ioctl(s->fd, SPI_IOC_MESSAGE(1), &t) < 1 ? -1 : 0;
}

#else   /* not Linux: the library still builds, the port does not open */

int ic_spi_open(struct ic_spi *s, const char *path, uint32_t hz, uint8_t mode)
{
    (void)path; (void)hz; (void)mode;
    if (s)
        s->fd = -1;
    return IC_ERR_IO;
}

void ic_spi_close(struct ic_spi *s) { (void)s; }

static int spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)ctx; (void)tx; (void)rx; (void)len;
    return -1;
}

#endif

struct ic_transport ic_spi_transport(struct ic_spi *s)
{
    struct ic_transport tr;

    tr.xfer = spi_xfer;
    tr.ctx  = s;
    return tr;
}
