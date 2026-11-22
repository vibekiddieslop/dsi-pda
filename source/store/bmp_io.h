// 4-bit (16 color) uncompressed BMP files, the format sketches are saved
// in so they open in any image viewer on a PC. Plain C; tests/host covers it.
#ifndef STORE_BMP_IO_H
#define STORE_BMP_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// pix: w * h palette indices (0..15), top row first. rgb: ncolors entries
// of 0xRRGGBB (the rest of the 16-entry palette is black). Returns a
// malloc'd file image and its size in *len, or NULL when out of memory.
uint8_t *bmp4_encode(const uint8_t *pix, int w, int h, const uint32_t *rgb, int ncolors,
                     size_t *len);

// Reads a 4-bit BMP of exactly w x h (top-down or bottom-up) into pix.
// Returns false when the data is not such a file.
bool bmp4_decode(const uint8_t *data, size_t len, uint8_t *pix, int w, int h);

#endif
