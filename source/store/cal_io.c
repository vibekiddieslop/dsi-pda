#include "cal_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cal_clear(CalList *c)
{
    c->count = 0;
}

static bool valid_repeat(char r)
{
    return r == '-' || r == 'D' || r == 'W' || r == 'M' || r == 'Y' || r == 'B' || r == 'A';
}

static void parse_line(char *line, void *ctx)
{
    CalList *c = ctx;
    char *s = text_trim(line);
    if (*s == '\0' || *s == '#' || c->count >= CAL_MAX)
        return;

    char *f[4] = { NULL };
    f[0] = s;
    for (int i = 1; i < 4; i++)
    {
        char *bar = strchr(f[i - 1], '|');
        if (bar == NULL)
            return; // Needs all 4 fields.
        *bar = '\0';
        f[i] = bar + 1;
    }

    CalEvent *e = &c->ev[c->count];
    // Date, or "start..end" for several days.
    char *ds = text_trim(f[0]);
    char *dots = strstr(ds, "..");
    if (dots)
        *dots = '\0';
    if (!date_parse(text_trim(ds), &e->start))
        return;
    e->end = e->start;
    if (dots)
    {
        Date end;
        if (!date_parse(text_trim(dots + 2), &end))
            return;
        long span = date_to_days(end) - date_to_days(e->start);
        if (span < 0 || span > CAL_SPAN_MAX)
            return;
        e->end = end;
    }

    // Time: "--:--", "HH:MM" or "HH:MM-HH:MM".
    char *t = text_trim(f[1]);
    e->end_minute = CAL_NO_END;
    if (*t == '\0' || strcmp(t, "--:--") == 0)
        e->minute = CAL_ALL_DAY;
    else
    {
        char *dash = strchr(t, '-');
        if (dash)
            *dash = '\0';
        if ((e->minute = time_parse_hhmm(text_trim(t))) < 0)
            return;
        if (dash && (e->end_minute = time_parse_hhmm(text_trim(dash + 1))) < 0)
            return;
    }

    char *r = text_trim(f[2]);
    e->repeat = (char)(r[0] >= 'a' && r[0] <= 'z' ? r[0] - 32 : r[0]);
    if (e->repeat == '\0')
        e->repeat = '-';
    if (!valid_repeat(e->repeat) || (r[0] != '\0' && r[1] != '\0'))
        return;

    // The text is the last field, so it may hold '|'.
    text_copy_clean(e->text, text_trim(f[3]), CAL_TEXT_MAX, false);
    c->count++;
}

void cal_parse(CalList *c, char *text)
{
    cal_clear(c);
    text_each_line(text, parse_line, c);
}

bool cal_serialize(const CalList *c, TextBuf *b)
{
    textbuf_append(b, "# PDA calendar. date[..end]|time[-end] or --:--|repeat - D W M Y B A"
                      "|text\n");
    for (int i = 0; i < c->count; i++)
    {
        const CalEvent *e = &c->ev[i];
        char d[11], de[11];
        date_format(e->start, d);
        if (cal_span(e) > 0)
        {
            date_format(e->end, de);
            textbuf_appendf(b, "%s..%s|", d, de);
        }
        else
        {
            textbuf_appendf(b, "%s|", d);
        }
        if (e->minute == CAL_ALL_DAY)
            textbuf_append(b, "--:--");
        else if (e->end_minute != CAL_NO_END)
            textbuf_appendf(b, "%02d:%02d-%02d:%02d", e->minute / 60, e->minute % 60,
                            e->end_minute / 60, e->end_minute % 60);
        else
            textbuf_appendf(b, "%02d:%02d", e->minute / 60, e->minute % 60);
        textbuf_appendf(b, "|%c|%s\n", e->repeat, e->text);
    }
    return !b->failed;
}

int cal_span(const CalEvent *e)
{
    long s = date_to_days(e->end) - date_to_days(e->start);
    return s > 0 ? (int)(s > CAL_SPAN_MAX ? CAL_SPAN_MAX : s) : 0;
}

// The event (or one of its repeats) starts on `day`.
static bool starts_on(const CalEvent *e, Date day)
{
    if (date_cmp(day, e->start) < 0)
        return false;
    switch (e->repeat)
    {
        case 'D':
            return true;
        case 'W':
            return (date_to_days(day) - date_to_days(e->start)) % 7 == 0;
        case 'M':
            return day.d == e->start.d;
        case 'Y':
        case 'B':
        case 'A':
            return day.m == e->start.m && day.d == e->start.d;
        default:
            return date_cmp(day, e->start) == 0;
    }
}

int cal_day_index(const CalEvent *e, Date day)
{
    int span = cal_span(e);
    for (int k = 0; k <= span; k++)
        if (starts_on(e, date_add_days(day, -k)))
            return k;
    return -1;
}

bool cal_occurs_on(const CalEvent *e, Date day)
{
    return cal_span(e) == 0 ? starts_on(e, day) : cal_day_index(e, day) >= 0;
}

void cal_time_text(const CalEvent *e, Date day, char *out, int size)
{
    if (e->minute == CAL_ALL_DAY)
    {
        snprintf(out, (size_t)size, "all");
        return;
    }
    char st[12], en[12];
    time_fmt12(e->minute, TIME_SHORT, st, sizeof(st));
    bool has_end = e->end_minute != CAL_NO_END;
    if (has_end)
        time_fmt12(e->end_minute, TIME_SHORT, en, sizeof(en));
    int span = cal_span(e);
    int k = span ? cal_day_index(e, day) : 0;
    if (span == 0 || k < 0)
    {
        if (has_end)
            snprintf(out, (size_t)size, "%s-%s", st, en);
        else
            snprintf(out, (size_t)size, "%s", st);
    }
    else if (k == 0)
        snprintf(out, (size_t)size, "%s-", st);
    else if (k == span && has_end)
        snprintf(out, (size_t)size, "-%s", en);
    else
        snprintf(out, (size_t)size, "all");
}

static const CalList *sort_list;
static Date sort_day;

// On a later day of a multi-day event its start time is not that day's:
// it sorts with the all-day events.
static int sort_minute(const CalEvent *e)
{
    if (cal_span(e) > 0 && cal_day_index(e, sort_day) > 0)
        return CAL_ALL_DAY;
    return e->minute;
}

static int cmp_idx(const void *a, const void *b)
{
    int mx = sort_minute(&sort_list->ev[*(const int *)a]);
    int my = sort_minute(&sort_list->ev[*(const int *)b]);
    if (mx != my)
        return mx < my ? -1 : 1;
    return *(const int *)a - *(const int *)b;
}

int cal_events_on(const CalList *c, Date day, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < c->count && n < max; i++)
    {
        if (cal_occurs_on(&c->ev[i], day))
            out[n++] = i;
    }
    sort_list = c;
    sort_day = day;
    qsort(out, (size_t)n, sizeof(int), cmp_idx);
    return n;
}

int cal_add(CalList *c, Date day, int minute, char repeat, const char *text)
{
    if (c->count >= CAL_MAX || !date_valid(day))
        return -1;
    CalEvent *e = &c->ev[c->count];
    e->start = day;
    e->end = day;
    e->minute = minute;
    e->end_minute = CAL_NO_END;
    e->repeat = valid_repeat(repeat) ? repeat : '-';
    text_copy_clean(e->text, text, CAL_TEXT_MAX, false);
    return c->count++;
}

void cal_remove(CalList *c, int index)
{
    if (index < 0 || index >= c->count)
        return;
    memmove(&c->ev[index], &c->ev[index + 1],
            sizeof(CalEvent) * (size_t)(c->count - index - 1));
    c->count--;
}

char cal_next_repeat(char r)
{
    switch (r)
    {
        case '-': return 'D';
        case 'D': return 'W';
        case 'W': return 'M';
        case 'M': return 'Y';
        case 'B': return 'A';
        case 'A': return 'B';
        default: return '-';
    }
}

bool cal_is_person(const CalEvent *e)
{
    return e->repeat == 'B' || e->repeat == 'A';
}

int cal_years_on(const CalEvent *e, Date day)
{
    if (!cal_is_person(e) || e->start.y == CAL_YEAR_UNKNOWN)
        return -1;
    int years = day.y - e->start.y;
    if (day.m < e->start.m || (day.m == e->start.m && day.d < e->start.d))
        years--;   // before this year's date
    return years >= 0 ? years : -1;
}

static const char *ordinal(int n)
{
    if (n % 100 >= 11 && n % 100 <= 13)
        return "th";
    switch (n % 10)
    {
        case 1: return "st";
        case 2: return "nd";
        case 3: return "rd";
        default: return "th";
    }
}

void cal_label(const CalEvent *e, Date day, char *out, int size)
{
    // On its date the birthday has arrived; any other day shows the age
    // reached at the next one (so "in 5 days" reads right).
    Date next = { day.y, e->start.m, e->start.d };
    if (date_cmp(next, day) < 0)
        next.y++;
    int years = cal_years_on(e, next);
    if (e->repeat == 'B')
    {
        if (years > 0)
            snprintf(out, (size_t)size, "%s turns %d", e->text, years);
        else
            snprintf(out, (size_t)size, "%s's birthday", e->text);
    }
    else if (e->repeat == 'A')
    {
        if (years > 0)
            snprintf(out, (size_t)size, "%s: %d%s anniversary", e->text, years, ordinal(years));
        else
            snprintf(out, (size_t)size, "%s: anniversary", e->text);
    }
    else
    {
        // "Trip (2/3)" on the second of three days.
        int span = cal_span(e);
        int k = span ? cal_day_index(e, day) : -1;
        if (k >= 0)
            snprintf(out, (size_t)size, "%s (%d/%d)", e->text, k + 1, span + 1);
        else
            snprintf(out, (size_t)size, "%s", e->text);
    }
}

bool cal_parse_person(const char *input, char *name, int name_size, int *year)
{
    char tmp[CAL_TEXT_MAX * 2 + 1];
    snprintf(tmp, sizeof(tmp), "%s", input);
    char *s = text_trim(tmp);
    *year = CAL_YEAR_UNKNOWN;
    // The last word is the year when it is 4 digits (1900..2999).
    char *last = strrchr(s, ' ');
    char *word = last ? last + 1 : s;
    if (strlen(word) == 4)
    {
        int y = 0;
        bool digits = true;
        for (int i = 0; i < 4; i++)
        {
            if (word[i] < '0' || word[i] > '9')
                digits = false;
            else
                y = y * 10 + (word[i] - '0');
        }
        if (digits && y >= 1900 && y <= 2999)
        {
            *year = y;
            *word = '\0';
            s = text_trim(s);
        }
    }
    if (*s == '\0')
        return false;
    text_copy_clean(name, s, (size_t)name_size - 1, false);
    return true;
}

// Reads a time at s: "H:MM" or "HH:MM" (24-hour), or 12-hour with a/p:
// "1:30p", "1:30pm", "5p", "12a". Returns the characters used, 0 if none.
static int read_hm(const char *s, int *minute)
{
    int i = 0, h = 0, m = 0;
    while (i < 2 && s[i] >= '0' && s[i] <= '9')
        h = h * 10 + (s[i++] - '0');
    if (i == 0)
        return 0;
    bool colon = false;
    if (s[i] == ':')
    {
        if (s[i + 1] < '0' || s[i + 1] > '9' || s[i + 2] < '0' || s[i + 2] > '9')
            return 0;
        m = (s[i + 1] - '0') * 10 + (s[i + 2] - '0');
        i += 3;
        colon = true;
    }
    char ap = (char)(s[i] | 32);
    if (ap == 'a' || ap == 'p')
    {
        if (h < 1 || h > 12)
            return 0;
        h = (h % 12) + (ap == 'p' ? 12 : 0);
        i++;
        if ((s[i] | 32) == 'm')
            i++;
    }
    else if (!colon)
    {
        return 0;   // a bare number is text ("2 eggs"), not a time
    }
    if (h > 23 || m > 59)
        return 0;
    *minute = h * 60 + m;
    return i;
}

bool cal_parse_entry(const char *input, int *minute, int *end_minute, char *text,
                     int text_size)
{
    char tmp[CAL_TEXT_MAX * 2 + 1];
    snprintf(tmp, sizeof(tmp), "%s", input);
    char *s = text_trim(tmp);

    *minute = CAL_ALL_DAY;
    *end_minute = CAL_NO_END;
    // "H:MM" or "HH:MM", optionally "-H:MM", then a space or the end.
    int m = 0, e = CAL_NO_END, n = read_hm(s, &m);
    if (n > 0)
    {
        int used = n, en = 0;
        if (s[n] == '-' && (en = read_hm(s + n + 1, &e)) > 0)
            used = n + 1 + en;
        if (s[used] == ' ' || s[used] == '\0')
        {
            *minute = m;
            if (used > n)
                *end_minute = e;
            s += used;
        }
        // Otherwise not a time after all; the whole input is the text.
    }
    s = text_trim(s);
    if (*s == '\0')
        return false;
    text_copy_clean(text, s, (size_t)text_size - 1, false);
    return true;
}
