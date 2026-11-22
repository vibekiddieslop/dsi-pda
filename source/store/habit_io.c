#include "habit_io.h"

#include <stdio.h>
#include <string.h>

void habit_clear(HabitList *l)
{
    l->count = 0;
}

static void parse_line(char *line, void *ctx)
{
    HabitList *l = ctx;
    char *s = text_trim(line);
    if (*s == '\0' || *s == '#' || l->count >= HABIT_MAX)
        return;
    char *f[4] = { s, NULL, NULL, NULL };
    for (int i = 1; i < 3; i++)
    {
        char *bar = strchr(f[i - 1], '|');
        if (bar == NULL)
            return;
        *bar = '\0';
        f[i] = bar + 1;
    }
    char *bar = strchr(f[2], '|');   // optional goal "N/w"
    if (bar)
    {
        *bar = '\0';
        f[3] = bar + 1;
    }
    Habit *h = &l->h[l->count];
    h->goal = 0;
    if (f[3])
    {
        const char *g = text_trim(f[3]);
        if (g[0] >= '1' && g[0] <= '0' + HABIT_GOAL_MAX && g[1] == '/' && (g[2] | 32) == 'w')
            h->goal = g[0] - '0';
    }
    text_copy_clean(h->name, text_trim(f[0]), HABIT_NAME_MAX, true);
    if (h->name[0] == '\0' || !date_parse(text_trim(f[1]), &h->start))
        return;
    const char *d = text_trim(f[2]);
    size_t n = strlen(d);
    // Keep the newest HABIT_DAYS days of an over-long history.
    size_t skip = n > HABIT_DAYS ? n - HABIT_DAYS : 0;
    h->start = date_add_days(h->start, (long)skip);
    h->len = 0;
    for (size_t i = skip; i < n; i++)
        h->days[h->len++] = (d[i] == 'x' || d[i] == 'X') ? 'x' : '-';
    h->days[h->len] = '\0';
    l->count++;
}

void habit_parse(HabitList *l, char *text)
{
    habit_clear(l);
    text_each_line(text, parse_line, l);
}

bool habit_serialize(const HabitList *l, TextBuf *b)
{
    textbuf_append(b, "# PDA habits. name|first day|one char per day: x done, - not"
                      "|optional goal N/w (N times a week)\n");
    for (int i = 0; i < l->count; i++)
    {
        const Habit *h = &l->h[i];
        char d[11];
        date_format(h->start, d);
        if (h->goal > 0)
            textbuf_appendf(b, "%s|%s|%s|%d/w\n", h->name, d, h->days, h->goal);
        else
            textbuf_appendf(b, "%s|%s|%s\n", h->name, d, h->days);
    }
    return !b->failed;
}

int habit_add(HabitList *l, const char *name, Date today)
{
    if (l->count >= HABIT_MAX)
        return -1;
    Habit *h = &l->h[l->count];
    char tmp[HABIT_NAME_MAX * 2 + 1];
    snprintf(tmp, sizeof(tmp), "%s", name);
    text_copy_clean(h->name, text_trim(tmp), HABIT_NAME_MAX, true);
    if (h->name[0] == '\0')
        return -1;
    h->start = today;
    h->goal = 0;
    h->len = 0;
    h->days[0] = '\0';
    return l->count++;
}

void habit_remove(HabitList *l, int index)
{
    if (index < 0 || index >= l->count)
        return;
    memmove(&l->h[index], &l->h[index + 1], sizeof(Habit) * (size_t)(l->count - index - 1));
    l->count--;
}

void habit_rename(Habit *h, const char *name)
{
    char tmp[HABIT_NAME_MAX * 2 + 1];
    snprintf(tmp, sizeof(tmp), "%s", name);
    char *t = text_trim(tmp);
    if (*t)
        text_copy_clean(h->name, t, HABIT_NAME_MAX, true);
}

bool habit_done(const Habit *h, Date day)
{
    long i = date_to_days(day) - date_to_days(h->start);
    return i >= 0 && i < h->len && h->days[i] == 'x';
}

void habit_set(Habit *h, Date day, bool done)
{
    long i = date_to_days(day) - date_to_days(h->start);
    if (i < 0)
    {
        // A day before the history: shift it right to make room.
        long shift = -i;
        if (h->len + shift > HABIT_DAYS)
            return;   // too far back to keep
        memmove(h->days + shift, h->days, (size_t)h->len + 1);
        memset(h->days, '-', (size_t)shift);
        h->len += (int)shift;
        h->start = day;
        i = 0;
    }
    if (i >= HABIT_DAYS)
    {
        // Past the kept window (say, a habit not touched for a year): drop
        // the oldest days first so day i fits.
        long drop = i - HABIT_DAYS + 1;
        if (drop >= h->len)
            h->len = 0;
        else
        {
            memmove(h->days, h->days + drop, (size_t)(h->len - drop));
            h->len -= (int)drop;
        }
        h->start = date_add_days(h->start, drop);
        i -= drop;
    }
    while (h->len <= i)
        h->days[h->len++] = '-';
    h->days[h->len] = '\0';
    h->days[i] = done ? 'x' : '-';
}

// Monday of the week holding `day`.
static Date week_start(Date day)
{
    return date_add_days(day, -((date_weekday(day) + 6) % 7));
}

int habit_week_count(const Habit *h, Date day)
{
    Date d = week_start(day);
    int n = 0;
    for (int i = 0; i < 7; i++, d = date_add_days(d, 1))
        n += habit_done(h, d);
    return n;
}

bool habit_satisfied(const Habit *h, Date today)
{
    return habit_done(h, today) || (h->goal > 0 && habit_week_count(h, today) >= h->goal);
}

int habit_streak(const Habit *h, Date today)
{
    if (h->goal > 0)
    {
        // Whole weeks meeting the goal, backwards from last week; this week
        // joins once it meets the goal (it is not over yet).
        Date w = week_start(today);
        int n = habit_week_count(h, w) >= h->goal ? 1 : 0;
        Date first = week_start(h->start);
        for (w = date_add_days(w, -7); date_cmp(w, first) >= 0; w = date_add_days(w, -7))
        {
            if (habit_week_count(h, w) < h->goal)
                break;
            n++;
        }
        return n;
    }
    Date d = habit_done(h, today) ? today : date_add_days(today, -1);
    int n = 0;
    while (habit_done(h, d))
    {
        n++;
        d = date_add_days(d, -1);
    }
    return n;
}

int habit_best_streak(const Habit *h, Date today)
{
    int best = 0, run = 0;
    if (h->goal > 0)
    {
        Date last = week_start(today);
        for (Date w = week_start(h->start); date_cmp(w, last) <= 0; w = date_add_days(w, 7))
        {
            run = habit_week_count(h, w) >= h->goal ? run + 1 : 0;
            if (run > best)
                best = run;
        }
        return best;
    }
    for (int i = 0; i < h->len; i++)
    {
        run = h->days[i] == 'x' ? run + 1 : 0;
        if (run > best)
            best = run;
    }
    return best;
}

int habit_done_last(const Habit *h, Date today, int n)
{
    int c = 0;
    Date d = today;
    for (int i = 0; i < n; i++, d = date_add_days(d, -1))
        c += habit_done(h, d);
    return c;
}

int habit_next_goal(int goal)
{
    return goal >= HABIT_GOAL_MAX ? 0 : goal + 1;
}

void habit_goal_text(int goal, char *out, int size)
{
    if (goal <= 0)
        snprintf(out, (size_t)size, "every day");
    else if (goal == 1)
        snprintf(out, (size_t)size, "once a week");
    else
        snprintf(out, (size_t)size, "%d times a week", goal);
}

int habit_done_count(const HabitList *l, Date day)
{
    int n = 0;
    for (int i = 0; i < l->count; i++)
        n += habit_satisfied(&l->h[i], day);
    return n;
}
