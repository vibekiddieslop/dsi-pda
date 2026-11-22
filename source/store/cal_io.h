// CAL.TXT: one event per line, fields split by '|'.
//   2026-11-20|14:00|-|Dentist
//   2026-01-05|--:--|Y|Sam's birthday
// Time "--:--" means all day. Repeat: '-' once, 'D' daily, 'W' weekly,
// 'M' monthly (skips months without that day), 'Y' yearly (Feb 29 only in
// leap years), 'B' birthday and 'A' anniversary: yearly, with the start
// date in the year of birth or wedding, so the age can be shown. Year 1904
// (CAL_YEAR_UNKNOWN) means the year is not known.
//   1996-03-14|--:--|B|Sam
//   1990-06-02|--:--|A|Mom & Dad
// A time range is "13:30-17:30". An event over several days has
// "start..end" as its date; a timed one starts on the first day and ends on
// the last. With a repeat, the whole span repeats.
//   2026-11-20|13:30-17:30|-|Workshop
//   2026-12-24..2026-12-27|--:--|Y|Holidays
#ifndef STORE_CAL_IO_H
#define STORE_CAL_IO_H

#include <stdbool.h>

#include "date.h"
#include "textio.h"

#define CAL_MAX 512
#define CAL_TEXT_MAX 100
#define CAL_ALL_DAY (-1)
#define CAL_NO_END (-1)
#define CAL_SPAN_MAX 62         // longest event in extra days
#define CAL_YEAR_UNKNOWN 1904   // a leap year, so Feb 29 birthdays fit

typedef struct {
    Date start;
    Date end;          // last day; equal to start for a one-day event
    int minute;        // Start, minutes since midnight, or CAL_ALL_DAY.
    int end_minute;    // End time, or CAL_NO_END (timed events only)
    char repeat;       // '-', 'D', 'W', 'M', 'Y', 'B', 'A'
    char text[CAL_TEXT_MAX + 1];
} CalEvent;

typedef struct {
    int count;
    CalEvent ev[CAL_MAX];
} CalList;

void cal_clear(CalList *c);
void cal_parse(CalList *c, char *text);
bool cal_serialize(const CalList *c, TextBuf *b);

bool cal_occurs_on(const CalEvent *e, Date day);
// Extra days an event lasts (0 for one day).
int cal_span(const CalEvent *e);
// Which day of its span `day` is (0 = first), or -1 when it is not on.
int cal_day_index(const CalEvent *e, Date day);
// The time column for `day`: "all", "14:00", "13:30-17:30"; over several
// days "13:30-" on the first day, "-17:30" on the last, "all" between.
void cal_time_text(const CalEvent *e, Date day, char *out, int size);
// Fills out[] with indices of events on `day`, all-day first, then by time.
// Returns the number written (at most max).
int cal_events_on(const CalList *c, Date day, int *out, int max);

int cal_add(CalList *c, Date day, int minute, char repeat, const char *text);
void cal_remove(CalList *c, int index);
// Cycles '-' -> 'D' -> 'W' -> 'M' -> 'Y' -> '-'; 'B' and 'A' swap.
char cal_next_repeat(char r);

// True for birthdays and anniversaries.
bool cal_is_person(const CalEvent *e);
// Years since the start on `day` for a birthday or anniversary with a known
// year, else -1.
int cal_years_on(const CalEvent *e, Date day);
// Display text for `day`: "Sam turns 30", "Sam's birthday" (year unknown),
// "Mom & Dad: 36th anniversary", or the plain text for other events.
void cal_label(const CalEvent *e, Date day, char *out, int size);
// Parses "Sam 1996" (the year is optional) into a name and a year
// (CAL_YEAR_UNKNOWN when missing). Returns false when the name is empty.
bool cal_parse_person(const char *input, char *name, int name_size, int *year);

// Parses editor input "HH:MM text", "HH:MM-HH:MM text" or "text" into the
// start minute (CAL_ALL_DAY when none), the end minute (CAL_NO_END when
// none) and the text. Returns false when the text part is empty.
bool cal_parse_entry(const char *input, int *minute, int *end_minute, char *text,
                     int text_size);

#endif
