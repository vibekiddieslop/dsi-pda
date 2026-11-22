// Handwriting recognition for one printed character at a time, using the
// $P point-cloud recognizer (Vatavu, Anthony, Wobbrock 2012): all strokes
// are resampled to one cloud of points, normalized for size and position,
// and matched against templates. Stroke order and direction do not matter,
// so an E drawn in one stroke or four matches the same template.
// Plain C, integer math; tests/host covers it.
#ifndef INPUT_HWR_H
#define INPUT_HWR_H

#include <stdbool.h>

#include "../store/textio.h"

#define HWR_MAX_POINTS 512
#define HWR_MAX_STROKES 8

typedef struct {
    short x, y;
    unsigned char stroke;   // 0-based stroke number
} InkPt;

typedef enum {
    HWR_LETTERS,   // a-z (upper and lower case shapes give the same letter), ? !
    HWR_DIGITS,    // 0-9 - + / : .
} HwrSet;

// Prepares the built-in templates. Call once.
void hwr_init(void);

// Recognizes one character drawn as points (pixels, y down). Writes up to
// max candidates to out, best first, and returns how many. A single tiny
// stroke is '.'.
int hwr_recognize(const InkPt *pts, int n, HwrSet set, char *out, int max);

// Gestures checked before character recognition: a long, flat, single
// stroke. Returns ' ' (left to right), '\b' (right to left) or 0.
char hwr_gesture(const InkPt *pts, int n, int pad_width);

// ---- the user's own samples (Settings > Train handwriting, and the pad's
// corrections). Each is a template like the built-in ones; recognition
// weighs them a little above the built-ins, so the user's way of writing
// a letter wins over the textbook shape.
#define HWR_USER_MAX 320
#define HWR_USER_PER_CHAR 8      // more replaces that character's oldest

// The set a character is written in (HWR_LETTERS or HWR_DIGITS), or -1
// when it cannot be trained ('.' is found by size, not shape). Letters are
// the same for both cases: pass the lowercase one.
int hwr_set_of(char ch);
// Adds a sample of ch. False when ch cannot be trained or the ink is a
// dot or empty.
bool hwr_user_add(char ch, const InkPt *pts, int n);
int hwr_user_count(char ch);
int hwr_user_total(void);
// Forgets ch's samples, or all of them for ch == 0.
void hwr_user_forget(char ch);
// Takes back the newest sample (a correction that was itself corrected).
void hwr_user_drop_last(void);
// True after a change not yet written out (hwr_user_serialize clears it).
bool hwr_user_dirty(void);

// The text form, one sample a line: "a|x,y x,y ..." with HWR's 32
// normalized points. Lines that do not fit (another point count, a bad
// character) are skipped.
void hwr_user_parse(char *text);
bool hwr_user_serialize(TextBuf *b);

#endif
