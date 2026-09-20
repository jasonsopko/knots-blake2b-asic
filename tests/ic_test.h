/* Minimal test scaffolding: no framework, only a counter and a macro. */
#ifndef IC_TEST_H
#define IC_TEST_H

#include <stdio.h>
#include <string.h>

static int ic_test_fails;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ic_test_fails++;                                             \
        }                                                                \
    } while (0)

#define CHECK_EQ(got, want)                                              \
    do {                                                                 \
        long g_ = (long)(got), w_ = (long)(want);                        \
        if (g_ != w_) {                                                  \
            printf("  FAIL %s:%d  %s: got %ld (0x%lx), want %ld (0x%lx)\n", \
                   __FILE__, __LINE__, #got, g_, (unsigned long)g_,      \
                   w_, (unsigned long)w_);                               \
            ic_test_fails++;                                             \
        }                                                                \
    } while (0)

#define TEST_MAIN(name, body)                                            \
    int main(void)                                                       \
    {                                                                    \
        printf("%s\n", name);                                            \
        body                                                             \
        if (ic_test_fails)                                               \
            printf("%s: %d failure(s)\n", name, ic_test_fails);          \
        return ic_test_fails ? 1 : 0;                                    \
    }

#endif
