// ALARMS.TXT: one daily alarm per line.
//   07:30|MTWTF--|on|Wake up
// The 7 day letters run Monday..Sunday; '-' skips that day.
#ifndef STORE_ALARM_IO_H
#define STORE_ALARM_IO_H

#include <stdbool.h>

#include "textio.h"

#define ALARM_MAX 16
#define ALARM_LABEL_MAX 40

typedef struct {
    int minute;                  // Minutes since midnight.
    char days[8];                // "MTWTFSS" with '-' for skipped days.
    bool on;
    char label[ALARM_LABEL_MAX + 1];
} Alarm;

typedef struct {
    int count;
    Alarm a[ALARM_MAX];
} AlarmList;

void alarm_clear(AlarmList *l);
void alarm_parse(AlarmList *l, char *text);
bool alarm_serialize(const AlarmList *l, TextBuf *b);

// weekday: 0 = Sunday ... 6 = Saturday.
bool alarm_on_weekday(const Alarm *a, int weekday);

int alarm_add(AlarmList *l, const Alarm *a);
void alarm_remove(AlarmList *l, int index);

#endif
