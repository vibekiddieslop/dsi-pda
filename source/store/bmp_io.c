#include "bmp_io.h"

#include <stdlib.h>

#define HEADER_SIZE (14 + 40 + 16 * 4)

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

static uint32_t get16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return get16(p) | (get16(p + 2) << 16);
}

static int row_bytes(int w)
{
    return (((w + 1) / 2) + 3) & ~3;   // two pixels per byte, 4-byte rows
}

uint8_t *bmp4_encode(const uint8_t *pix, int w, int h, const uint32_t *rgb, int ncolors,
                     size_t *len)
{
    int rb = row_bytes(w);
    size_t size = HEADER_SIZE + (size_t)rb * (size_t)h;
    uint8_t *f = calloc(1, size);
    if (f == NULL)
        return NULL;

    f[0] = 'B';
    f[1] = 'M';
    put32(f + 2, (uint32_t)size);
    put32(f + 10, HEADER_SIZE);
    put32(f + 14, 40);
    put32(f + 18, (uint32_t)w);
    put32(f + 22, (uint32_t)h);       // positive: rows stored bottom-up
    put16(f + 26, 1);                 // planes
    put16(f + 28, 4);                 // bits per pixel
    put32(f + 30, 0);                 // no compression
    put32(f + 34, (uint32_t)(rb * h));
    put32(f + 38, 2835);              // 72 dpi
    put32(f + 42, 2835);
    put32(f + 46, 16);
    for (int i = 0; i < 16; i++)
    {
        uint32_t c = i < ncolors ? rgb[i] : 0;
        uint8_t *e = f + 54 + i * 4;
        e[0] = (uint8_t)c;            // B
        e[1] = (uint8_t)(c >> 8);     // G
        e[2] = (uint8_t)(c >> 16);    // R
    }
    for (int y = 0; y < h; y++)
    {
        const uint8_t *src = pix + (size_t)y * (size_t)w;
        uint8_t *dst = f + HEADER_SIZE + (size_t)(h - 1 - y) * (size_t)rb;
        for (int x = 0; x < w; x++)
        {
            uint8_t v = src[x] & 0x0F;
            dst[x / 2] |= (x & 1) ? v : (uint8_t)(v << 4);
        }
    }
    *len = size;
    return f;
}

bool bmp4_decode(const uint8_t *data, size_t len, uint8_t *pix, int w, int h)
{
    if (len < HEADER_SIZE || data[0] != 'B' || data[1] != 'M')
        return false;
    uint32_t offset = get32(data + 10);
    int32_t bw = (int32_t)get32(data + 18);
    int32_t bh = (int32_t)get32(data + 22);
    if (get16(data + 28) != 4 || get32(data + 30) != 0 || bw != w || (bh != h && bh != -h))
        return false;
    int rb = row_bytes(w);
    if (offset > len || len - offset < (size_t)rb * (size_t)h)
        return false;
    bool bottom_up = bh > 0;
    for (int y = 0; y < h; y++)
    {
        const uint8_t *src = data + offset + (size_t)(bottom_up ? h - 1 - y : y) * (size_t)rb;
        uint8_t *dst = pix + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++)
            dst[x] = (x & 1) ? (src[x / 2] & 0x0F) : (src[x / 2] >> 4);
    }
    return true;
}
