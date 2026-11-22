// Work shifts: the user's own shift types, kept in SHIFTS.TXT, one a line:
//   Late shift|12:30-19:00|11:00-17:30
// The name, the weekday hours (Mon-Fri) and, optionally, other hours for
// Saturday and Sunday. A fresh install has none; the custom time slot
// (hours picked each time) is always there.
//
// A shift on the calendar is a normal event whose text starts with "Work: "
// (CAL.TXT stays plain text), e.g. "2026-11-23|10:00-16:30|-|Work: Early".
#ifndef STORE_WORK_H
#define STORE_WORK_H

#include <stdbool.h>

#include "cal_io.h"
#include "date.h"
#include "textio.h"

#define SHIFT_MAX 7               // the picker shows them all, with two more keys
#define SHIFT_NAME_MAX 20
#define SHIFT_CUSTOM (-1)         // the custom time slot
#define SHIFT_CUSTOM_NAME "Custom time slot"
#define SHIFT_SAME (-1)           // weekend hours: same as weekdays

typedef struct {
    char name[SHIFT_NAME_MAX + 1];
    int start, end;               // weekdays, minutes since midnight
    int we_start, we_end;         // Sat and Sun, or SHIFT_SAME
} Shift;

typedef struct {
    int count;
    Shift s[SHIFT_MAX];
} ShiftList;

#define WORK_PREFIX "Work: "

void shift_clear(ShiftList *l);
void shift_parse(ShiftList *l, char *text);
bool shift_serialize(const ShiftList *l, TextBuf *b);
// Returns the new index, or -1 when the list is full.
int shift_add(ShiftList *l, const Shift *s);
void shift_remove(ShiftList *l, int index);

// Start and end on `day` (the weekend hours on Saturday and Sunday).
void shift_hours(const Shift *s, Date day, int *start, int *end);
// "12:30p-7p Late shift" -> the weekday hours and the name; the weekend
// hours become SHIFT_SAME. False without both times or without a name.
bool shift_parse_entry(const char *input, Shift *out);
// "11a-5:30p" alone -> start and end. False for anything else.
bool shift_parse_range(const char *input, int *start, int *end);
// "12:30p-7p" (the form shift_parse_entry reads too).
void shift_range_text(int start, int end, char *out, int size);

// True for an event made from a shift.
bool cal_is_work(const CalEvent *e);

#endif
