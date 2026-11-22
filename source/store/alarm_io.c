#include "alarm_io.h"

#include <string.h>

#include "date.h"

static const char DAY_LETTERS[8] = "MTWTFSS";

void alarm_clear(AlarmList *l)
{
    l->count = 0;
}

// Accepts exactly 7 characters where position i is DAY_LETTERS[i] (any case)
// or '-'.
static bool parse_days(const char *s, char out[8])
{
    if (strlen(s) != 7)
        return false;
    for (int i = 0; i < 7; i++)
    {
        char c = s[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        if (c != '-' && c != DAY_LETTERS[i])
            return false;
        out[i] = c;
    }
    out[7] = '\0';
    return true;
}

static void parse_line(char *line, void *ctx)
{
    AlarmList *l = ctx;
    char *s = text_trim(line);
    if (*s == '\0' || *s == '#' || l->count >= ALARM_MAX)
        return;

    char *f[4] = { s, NULL, NULL, NULL };
    for (int i = 1; i < 4; i++)
    {
        char *bar = strchr(f[i - 1], '|');
        if (bar == NULL)
            return;
        *bar = '\0';
        f[i] = bar + 1;
    }

    Alarm *a = &l->a[l->count];
    a->minute = time_parse_hhmm(text_trim(f[0]));
    if (a->minute < 0 || !parse_days(text_trim(f[1]), a->days))
        return;
    char *on = text_trim(f[2]);
    if (strcmp(on, "on") == 0)
        a->on = true;
    else if (strcmp(on, "off") == 0)
        a->on = false;
    else
        return;
    text_copy_clean(a->label, text_trim(f[3]), ALARM_LABEL_MAX, true);
    l->count++;
}

void alarm_parse(AlarmList *l, char *text)
{
    alarm_clear(l);
    text_each_line(text, parse_line, l);
}

bool alarm_serialize(const AlarmList *l, TextBuf *b)
{
    textbuf_append(b, "# PDA alarms. HH:MM|MTWTFSS (- = skip day)|on/off|label\n");
    for (int i = 0; i < l->count; i++)
    {
        const Alarm *a = &l->a[i];
        textbuf_appendf(b, "%02d:%02d|%s|%s|%s\n", a->minute / 60, a->minute % 60,
                        a->days, a->on ? "on" : "off", a->label);
    }
    return !b->failed;
}

bool alarm_on_weekday(const Alarm *a, int weekday)
{
    int idx = (weekday + 6) % 7; // Sunday (0) -> index 6
    return a->days[idx] != '-';
}

int alarm_add(AlarmList *l, const Alarm *a)
{
    if (l->count >= ALARM_MAX)
        return -1;
    l->a[l->count] = *a;
    return l->count++;
}

void alarm_remove(AlarmList *l, int index)
{
    if (index < 0 || index >= l->count)
        return;
    memmove(&l->a[index], &l->a[index + 1], sizeof(Alarm) * (size_t)(l->count - index - 1));
    l->count--;
}
