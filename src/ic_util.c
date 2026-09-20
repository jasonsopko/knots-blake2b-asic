/* ic_util.c - odds and ends. */
#include <string.h>

#include "intchains.h"

const char *ic_strerror(int err)
{
    switch (err) {
    case IC_OK:          return "ok";
    case IC_ERR_ARG:     return "bad argument";
    case IC_ERR_IO:      return "transport failed";
    case IC_ERR_NOREPLY: return "no matching reply in the receive buffer";
    case IC_ERR_CRC:     return "reply checksum mismatch";
    case IC_ERR_RANGE:   return "value out of range";
    case IC_ERR_STATE:   return "chain not ready";
    default:             return "unknown error";
    }
}

void ic_chain_init(struct ic_chain *ch, const struct ic_family *fam,
                   struct ic_transport tr)
{
    memset(ch, 0, sizeof(*ch));
    ch->fam = fam;
    ch->tr  = tr;
}
