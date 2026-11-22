// One-line text entry. The bottom screen holds either the big-key keyboard
// (tap or swipe words) or the handwriting pad; [ui] input=keys/pen in
// CONFIG.INI remembers which one was used last.
#ifndef UI_EDITOR_H
#define UI_EDITOR_H

#include <stdbool.h>

#define EDITOR_MAX 200

// Called after the editor closes. text is valid only during the call.
typedef void (*EditorDone)(bool ok, const char *text);

void editor_init(void);   // Once at startup, after gfx_init() and app_init().
// The handwriting samples (pda/HWR.TXT): loaded by editor_init; saved when
// they changed, as an edit or the training closes and before a launch.
void handwriting_load(void);
void handwriting_save(void);
// The input surface text entry opens with: the handwriting pad, or the
// keyboard. Remembered as [ui] input= in CONFIG.INI.
bool editor_uses_pen(void);
void editor_set_pen(bool pen);
void editor_open(const char *title, const char *prompt, const char *initial, int max_len,
                 EditorDone done);
// The same for an amount: the keyboard opens on its 123 page, the
// handwriting pad in digit mode.
void editor_open_number(const char *title, const char *prompt, const char *initial,
                        int max_len, EditorDone done);

#endif
