#include "work.h"

#include <stdio.h>
#include <string.h>

void shift_clear(ShiftList *l)
{
    l->count = 0;
}

// "HH:MM-HH:MM" as written in the file.
static bool file_range(char *s, int *start, int *end)
{
    s = text_trim(s);
    char *dash = strchr(s, '-');
    if (dash == NULL)
        return false;
    *dash = '\0';
    *start = time_parse_hhmm(text_trim(s));
    *end = time_parse_hhmm(text_trim(dash + 1));
    return *start >= 0 && *end >= 0;
}

static void parse_line(char *line, void *ctx)
{
    ShiftList *l = ctx;
    char *s = text_trim(line);
    if (*s == '\0' || *s == '#' || l->count >= SHIFT_MAX)
        return;
    char *f[3] = { s, NULL, NULL };
    char *bar = strchr(s, '|');
    if (bar == NULL)
        return;
    *bar = '\0';
    f[1] = bar + 1;
    bar = strchr(f[1], '|');
    if (bar != NULL)
    {
        *bar = '\0';
        f[2] = bar + 1;
    }
    Shift *sh = &l->s[l->count];
    text_copy_clean(sh->name, text_trim(f[0]), SHIFT_NAME_MAX, true);
    if (sh->name[0] == '\0' || !file_range(f[1], &sh->start, &sh->end))
        return;
    sh->we_start = sh->we_end = SHIFT_SAME;
    if (f[2] != NULL && *text_trim(f[2]) != '\0' && !file_range(f[2], &sh->we_start, &sh->we_end))
        return;
    l->count++;
}

void shift_parse(ShiftList *l, char *text)
{
    shift_clear(l);
    text_each_line(text, parse_line, l);
}

bool shift_serialize(const ShiftList *l, TextBuf *b)
{
    textbuf_append(b, "# PDA work shifts. name|weekday HH:MM-HH:MM|optional Sat+Sun HH:MM-HH:MM\n");
    for (int i = 0; i < l->count; i++)
    {
        const Shift *s = &l->s[i];
        textbuf_appendf(b, "%s|%02d:%02d-%02d:%02d", s->name, s->start / 60, s->start % 60,
                        s->end / 60, s->end % 60);
        if (s->we_start != SHIFT_SAME)
            textbuf_appendf(b, "|%02d:%02d-%02d:%02d", s->we_start / 60, s->we_start % 60,
                            s->we_end / 60, s->we_end % 60);
        textbuf_append(b, "\n");
    }
    return !b->failed;
}

int shift_add(ShiftList *l, const Shift *s)
{
    if (l->count >= SHIFT_MAX)
        return -1;
    l->s[l->count] = *s;
    return l->count++;
}

void shift_remove(ShiftList *l, int index)
{
    if (index < 0 || index >= l->count)
        return;
    memmove(&l->s[index], &l->s[index + 1], sizeof(Shift) * (size_t)(l->count - index - 1));
    l->count--;
}

void shift_hours(const Shift *s, Date day, int *start, int *end)
{
    int wd = date_weekday(day);
    bool weekend = (wd == 0 || wd == 6) && s->we_start != SHIFT_SAME;
    *start = weekend ? s->we_start : s->start;
    *end = weekend ? s->we_end : s->end;
}

bool shift_parse_entry(const char *input, Shift *out)
{
    int m, e;
    char text[CAL_TEXT_MAX + 1];
    if (!cal_parse_entry(input, &m, &e, text, sizeof(text)) || m == CAL_ALL_DAY ||
        e == CAL_NO_END)
        return false;
    text_copy_clean(out->name, text, SHIFT_NAME_MAX, true);
    if (out->name[0] == '\0')
        return false;
    // The hours come first, so the keyboard's automatic capital never
    // reaches the name: "9a-5p office" is "Office".
    if (out->name[0] >= 'a' && out->name[0] <= 'z')
        out->name[0] = (char)(out->name[0] - 32);
    out->start = m;
    out->end = e;
    out->we_start = out->we_end = SHIFT_SAME;
    return true;
}

bool shift_parse_range(const char *input, int *start, int *end)
{
    // The entry parser needs text after the times: give it one word.
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s x", input);
    int m, e;
    char text[8];
    if (!cal_parse_entry(tmp, &m, &e, text, sizeof(text)) || m == CAL_ALL_DAY ||
        e == CAL_NO_END || strcmp(text, "x") != 0)
        return false;
    *start = m;
    *end = e;
    return true;
}

void shift_range_text(int start, int end, char *out, int size)
{
    char a[12], b[12];
    time_fmt12(start, TIME_SHORT, a, sizeof(a));
    time_fmt12(end, TIME_SHORT, b, sizeof(b));
    snprintf(out, (size_t)size, "%s-%s", a, b);
}

bool cal_is_work(const CalEvent *e)
{
    return strncmp(e->text, WORK_PREFIX, strlen(WORK_PREFIX)) == 0;
}
