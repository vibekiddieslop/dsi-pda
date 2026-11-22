// Text input surfaces for the editor's bottom screen: the big-key keyboard
// with swipe typing (kbd.c) and the handwriting pad (pad.c). Both draw into
// the bottom bitmap below the 20 px status strip and talk to the editor
// through a TextSink.
#ifndef UI_TEXTIN_H
#define UI_TEXTIN_H

#include "ui.h"

typedef struct {
    int (*insert)(const char *s);    // at the cursor; returns chars inserted
                                     // (fewer when the text is full)
    void (*backspace)(int n);        // n characters before the cursor
    void (*enter)(void);             // save and close
    char (*before)(int back);        // char at cursor - 1 - back, 0 if none
    void (*switch_mode)(void);       // keyboard <-> pad
} TextSink;

void kbd_init(void);                 // once: indexes the swipe word list
void kbd_reset(const TextSink *t);   // editor opened: letters page, auto caps
void kbd_update(const Input *in, const TextSink *t);
void kbd_draw(Surface *s);
// The editor changed the text or cursor itself (B, D-pad).
void kbd_text_edited(const TextSink *t);

void pad_init(void);                 // once: prepares handwriting templates
void pad_reset(const TextSink *t);
void pad_update(const Input *in, const TextSink *t);
void pad_draw(Surface *s);
void pad_text_edited(const TextSink *t);

// True while kbd_reset/pad_reset run for an amount (editor_open_number).
bool textin_numbers(void);

// Shared by both: true when the next letter should be a capital (start of
// the text or after ". ", "! ", "? ").
bool textin_sentence_start(const TextSink *t);

#endif
