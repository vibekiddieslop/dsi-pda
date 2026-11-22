// Jingles on the DS tone generators: short note sequences stepped once per
// frame. One voice plays effects (a new one replaces the one playing); a
// second voice loops the alarm melody.
#ifndef SFX_H
#define SFX_H

#include <stdbool.h>

typedef enum {
    SFX_TAP,       // a key or button
    SFX_OPEN,      // a screen or menu opens
    SFX_BACK,      // back, cancel
    SFX_STAMP,     // checked off
    SFX_UNCHECK,
    SFX_SAVED,     // added or saved
    SFX_DELETE,
    SFX_ERROR,     // not possible
    SFX_LAUNCH,    // leaving for a game or app
    SFX_TIMER,     // countdown started
    SFX_POP,       // a note pops up
    SFX_COUNT
} Sfx;

// [ui] sounds=off mutes the effects. The alarm melody always plays.
void sfx_set_enabled(bool on);
bool sfx_enabled(void);
void sfx_play(Sfx id);
// Starts or stops the looping alarm melody.
void sfx_alarm(bool on);
// Once per frame.
void sfx_tick(void);

#endif
