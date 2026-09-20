/* ic_device.c - the four back-ends, section 2. */
#include <string.h>

#include "intchains.h"

/* Cores per chip and the nonce ceiling are compile-time constants of the
 * controller build, not properties of the chip driver. ICC551 is compiled in
 * but no product binds it, so neither value exists anywhere to be read; both
 * are zero here and ic_work_build refuses to build for it.
 *
 * header_bytes is work_bytes minus the 16 byte trailer (64 bit target bound,
 * 64 bit nonce ceiling). On ICA586 the 16 bytes between the header and the
 * trailer are reserved and sent as zero. */
const struct ic_family ic_ict580 = {
    "ICT580", 80, 48, 96, 80, UINT64_C(0x0000FFFFFFFFFFFF),
    500000, 1, 0xFE00FC8Fu, 0
};
const struct ic_family ic_icc590 = {
    "ICC590", 20, 72, 144, 128, UINT64_C(0x0000FFFFFFFFFFFF),
    500000, 1, 0xFE00FC8Fu, 0
};
const struct ic_family ic_ica586 = {
    "ICA586", 20, 56, 112, 80, UINT64_C(0x00003FFFFFFFFFFF),
    500000, 1, 0xFE00FC8Fu, IC_QUIRK_TARGET_TAG_13DF
};
const struct ic_family ic_icc551 = {
    "ICC551", 0, 48, 96, 80, 0,
    500000, 1, 0xFE00FC8Fu, 0
};

static const struct ic_family *const families[] = {
    &ic_ict580, &ic_icc590, &ic_ica586, &ic_icc551
};

const struct ic_family *ic_family_by_name(const char *name)
{
    size_t i;

    if (!name)
        return NULL;
    for (i = 0; i < sizeof(families) / sizeof(families[0]); i++) {
        const char *a = families[i]->name, *b = name;
        size_t j;

        for (j = 0; a[j] && b[j]; j++) {
            char ca = a[j], cb = b[j];
            if (cb >= 'a' && cb <= 'z')
                cb = (char)(cb - 'a' + 'A');
            if (ca != cb)
                break;
        }
        if (!a[j] && !b[j])
            return families[i];
    }
    /* ICA590 is a configuration name with no back-end behind it. Products that
     * declare it run ICC590. Model strings are unreliable in general; this is
     * the one case documented well enough to resolve. */
    return NULL;
}
