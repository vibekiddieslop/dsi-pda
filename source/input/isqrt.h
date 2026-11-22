// Integer square root, rounded down: the ARM9 has no FPU. Shared by the
// swipe decoder and the handwriting recognizer.
#ifndef INPUT_ISQRT_H
#define INPUT_ISQRT_H

#include <stdint.h>

static inline uint32_t isqrt(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v)
        bit >>= 2;
    while (bit)
    {
        if (v >= r + bit)
        {
            v -= r + bit;
            r = (r >> 1) + bit;
        }
        else
        {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

#endif
