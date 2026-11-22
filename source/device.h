// Console hardware the PDA uses outside its data files: battery level,
// backlight brightness (L+R + Up/Down), the volume rocker, CPU speed, and
// the settings kept in CONFIG.INI.
#ifndef DEVICE_H
#define DEVICE_H

#include <stdbool.h>

#include "ui/ui.h"

// After app_init() (reads [ui] from CONFIG.INI) and soundEnable().
void device_init(void);
// Once per frame: steps the sounds, polls the battery and the volume.
void device_tick(void);

// Short tick sound for a tap or key, unless [ui] sounds=off.
void device_click(void);

// The owner's name for the greeting: [ui] name= in CONFIG.INI, else the
// console's profile name. "" when neither is set.
const char *device_user_name(void);

// Settings screen: backlight level 1..5 (0 = not known yet) and the sound
// effects switch. Both are remembered in CONFIG.INI; the level is written
// half a second after the last change, so holding a key does not write
// the card on every step.
int device_brightness(void);
void device_set_brightness(int level);
void device_set_sounds(bool on);
// US holidays on the calendar: [ui] holidays=on/off (on when missing).
bool device_holidays(void);
void device_set_holidays(bool on);
// THEME_*: recolors at once and is remembered as [ui] theme= in CONFIG.INI.
void device_set_theme(int theme);
// Writes a setting still waiting to be saved. Called before leaving the PDA.
void device_flush(void);

// Idle dimming: the backlight drops to its lowest level after a minute
// without input. This brings it back (an alarm calls it when it rings).
void device_wake(void);
// Full CPU speed (true) for typing, half speed otherwise. DSi mode only.
void device_cpu_fast(bool fast);

// Called by ui_frame on each frame's input before the screen sees it.
// Holding L+R makes Up/Down change the backlight; the shoulder presses that
// form the chord are not passed on, so screens never act on them. A single
// shoulder press reaches the screen a few frames late.
void device_filter_input(Input *in);

#endif
