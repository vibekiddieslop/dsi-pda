// Calendar arithmetic shared by the calendar, alarms and to-do due dates.
#ifndef STORE_DATE_H
#define STORE_DATE_H

#include <stdbool.h>

typedef struct {
    int y, m, d;   // m is 1..12, d is 1..31
} Date;

// Names, 0 = Sunday and 0 = January: "Sunday", "Sun", "January", "Jan".
extern const char *const DATE_DAY[7];
extern const char *const DATE_DAY3[7];
extern const char *const DATE_MONTH[12];
extern const char *const DATE_MONTH3[12];
// "Mo" .. "Su": the columns of a month grid, which starts on Monday.
extern const char *const DATE_GRID_DAY[7];

bool date_is_leap(int y);
int date_days_in_month(int y, int m);
bool date_valid(Date dt);
// 0 = Sunday ... 6 = Saturday
int date_weekday(Date dt);
// Days since 1970-01-01 (negative before it).
long date_to_days(Date dt);
Date date_from_days(long days);
Date date_add_days(Date dt, long n);
// Months later (or earlier for n < 0); the day is clamped to the month's
// length (Jan 31 + 1 month = Feb 28 or 29).
Date date_add_months(Date dt, int n);
int date_cmp(Date a, Date b);

// "YYYY-MM-DD" -> Date. Returns false on any format or range error.
bool date_parse(const char *s, Date *out);
// Writes "YYYY-MM-DD" (11 bytes with NUL).
void date_format(Date dt, char out[11]);

// "HH:MM" -> minutes since midnight. Returns -1 on error.
int time_parse_hhmm(const char *s);

// 12-hour clock text for minutes since midnight:
//   TIME_SHORT  "7p", "1:30p", "12p" (noon), "12a" (midnight)
//   TIME_MIN    "7:00p", "1:30p" (always with minutes)
//   TIME_LONG   "7:00 PM"
enum { TIME_SHORT, TIME_MIN, TIME_LONG };
void time_fmt12(int minute, int style, char *out, int size);

// A due date written relative to today, case-insensitive:
//   today, tod          tmrw, tmr, tomorrow, tom
//   mon .. sunday       the next such day after today (1..7 days ahead)
//   +N, +Nd, +Nw        N days or N weeks from today
//   MM-DD, M/D          this year, or next year when already past
//   YYYY-MM-DD          as is
// Returns false when the word is not understood.
bool date_parse_relative(const char *s, Date today, Date *out);

#endif
