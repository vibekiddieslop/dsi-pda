// Countdown timers, alarm checks, ringing and missed-alarm tracking.
// Alarms only ring while the lid is open. Alarms and timers that came due
// while the console slept are listed as missed when it wakes.
#ifndef TIMERS_H
#define TIMERS_H

#include <stdbool.h>
#include <time.h>

#include "ui/ui.h"

#define TIMER_MAX 4
#define MISSED_MAX 6

typedef struct {
    bool used;
    bool running;
    time_t end;       // Valid while running.
    int remaining;    // Seconds left while paused.
    int total;        // Original length in seconds.
    char label[24];   // What it rings as; "" = "5 min timer" (a snooze keeps the alarm's)
} Countdown;

extern Countdown g_timers[TIMER_MAX];
extern const Overlay g_ring_overlay;

void timers_init(time_t now);
// Call once per frame.
void timers_tick(time_t now);

int timers_add(int seconds, time_t now);    // Returns the index or -1.
#define SNOOZE_SECONDS (5 * 60)
void timers_toggle(int index, time_t now);
void timers_remove(int index);
int timers_left(const Countdown *c, time_t now);

int missed_count(void);
const char *missed_text(int index);
void missed_clear(void);

// "7:30a tmrw Wake up" for the next enabled alarm within 7 days.
bool timers_next_alarm(time_t now, char *out, int size);

#endif
