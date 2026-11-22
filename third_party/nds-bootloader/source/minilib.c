// Local addition for the BlocksDS build of load.bin.
// devkitARM linked newlib for these symbols. load.bin links with -nostdlib,
// so this file provides the 5 functions the loader calls.

#include <stddef.h>
#include <stdint.h>
#include <nds/ndstypes.h>
#include <nds/dma.h>

void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dest;
}

void *memset(void *dest, int c, size_t n)
{
    uint8_t *d = dest;
    while (n--)
        *d++ = (uint8_t)c;
    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++)
    {
        if (*x != *y)
            return *x - *y;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

void dmaSetParams(uint8_t channel, const void *src, void *dest, uint32_t ctrl)
{
    REG_DMA_SRC(channel) = (uint32_t)src;
    REG_DMA_DEST(channel) = (uint32_t)dest;
    REG_DMA_CR(channel) = ctrl;
}
