// US holidays and observances, worked out for any year (no file): fixed
// dates, the Nth weekday of a month, and Easter. Also the two daylight
// saving changes, since the DSi clock does not move by itself.
// Plain C; tests/host covers it.
#ifndef STORE_HOLIDAYS_H
#define STORE_HOLIDAYS_H

#include <stdbool.h>

#include "date.h"

// Names of the holidays on `day` (at most max), in calendar order.
// Returns how many.
int holidays_on(Date day, const char **out, int max);
// The first holiday in [from, from + days). False when there is none.
bool holiday_next(Date from, int days, Date *when, const char **name);
// Easter Sunday of a year (Gregorian).
Date holiday_easter(int year);

#endif
