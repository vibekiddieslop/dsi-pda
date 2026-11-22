// Swipe typing: one stroke across the letter keys becomes a word.
// Plain C, integer math only (the ARM9 has no FPU); tests/host covers it.
//
// The word list is "<rank><word>\n" lines (data/words.bin, rank 0 = most
// common). A stroke is matched against the ideal path through each
// candidate word's key centers: both are resampled to the same number of
// points and compared point by point. Candidates are words whose first
// letter is near the stroke's start and last letter near its end.
#ifndef INPUT_SWIPE_H
#define INPUT_SWIPE_H

#include <stdbool.h>
#include <stddef.h>

#define SWIPE_WORD_MAX 15
#define SWIPE_PATH_MAX 256

typedef struct {
    short x, y;
} SwipePt;

// Indexes the word list (kept by reference; it must stay valid). Returns
// false when out of memory.
bool swipe_load(const char *list, size_t len);
int swipe_word_count(void);

// Best matches for a stroke (bottom-screen pixels), best first. Returns the
// number written to out (0 when nothing fits).
int swipe_decode(const SwipePt *path, int n, char out[][SWIPE_WORD_MAX + 1], int max_out);

#endif
