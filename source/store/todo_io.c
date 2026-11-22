#include "todo_io.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

void todo_clear(TodoList *l)
{
    l->count = 0;
}

bool todo_parse_every(const char *s, char *unit, int *every)
{
    int n = 0, digits = 0;
    while (*s >= '0' && *s <= '9' && digits < 2)
    {
        n = n * 10 + (*s++ - '0');
        digits++;
    }
    if (digits == 0)
        n = 1;
    char u = (char)(*s | 32);
    if (n < 1 || (u != 'd' && u != 'w' && u != 'm' && u != 'y'))
        return false;
    // The rest must be letters of a unit word ("w", "week", "weekly"...).
    for (const char *p = s; *p; p++)
        if (!((*p | 32) >= 'a' && (*p | 32) <= 'z'))
            return false;
    *unit = u;
    *every = n;
    return true;
}

void todo_repeat_text(char unit, int every, char *out, int size)
{
    const char *name = unit == 'd' ? "day" : unit == 'w' ? "week" : unit == 'm' ? "month"
                     : unit == 'y' ? "year" : NULL;
    if (name == NULL)
        snprintf(out, (size_t)size, "never");
    else if (every <= 1)
        snprintf(out, (size_t)size, "every %s", name);
    else
        snprintf(out, (size_t)size, "every %d %ss", every, name);
}

Date todo_next_due(Date due, char unit, int every)
{
    if (every < 1)
        every = 1;
    switch (unit)
    {
        case 'd': return date_add_days(due, every);
        case 'w': return date_add_days(due, 7L * every);
        case 'm': return date_add_months(due, every);
        case 'y': return date_add_months(due, 12 * every);
        default: return due;
    }
}

bool todo_complete(TodoItem *it, Date today)
{
    if (it->repeat == 0)
    {
        it->done = true;
        return false;
    }
    // At least one step, then on until the date is after today: a late or
    // an early check both land on the next due date still ahead. Without a
    // date the repeat counts from today.
    Date d = it->has_due ? it->due : today;
    it->has_due = true;
    int guard = 0;
    do
        d = todo_next_due(d, it->repeat, it->every);
    while (date_cmp(d, today) <= 0 && ++guard < 10000);
    it->due = d;
    it->done = false;
    return true;
}

uint32_t todo_days_mask(const TodoItem *it, Date first, int ndays)
{
    if (!it->has_due || ndays <= 0)
        return 0;
    if (ndays > 32)
        ndays = 32;
    long f = date_to_days(first), end = f + ndays;
    Date d = it->due;
    long dd = date_to_days(d);
    if (it->repeat && dd < f)
    {
        if (it->repeat == 'd' || it->repeat == 'w')
        {
            // Fixed steps: jump straight to the first one in the window.
            long step = (long)(it->every < 1 ? 1 : it->every) * (it->repeat == 'w' ? 7 : 1);
            d = date_add_days(d, (f - dd + step - 1) / step * step);
        }
        else
        {
            for (int guard = 0; guard < 2400 && date_to_days(d) < f; guard++)
                d = todo_next_due(d, it->repeat, it->every);
        }
        dd = date_to_days(d);
    }
    uint32_t m = 0;
    for (int guard = 0; guard < 32 && dd < end; guard++)
    {
        if (dd >= f)
            m |= 1u << (dd - f);
        if (!it->repeat)
            break;
        d = todo_next_due(d, it->repeat, it->every);
        dd = date_to_days(d);
    }
    return m;
}

static int sort_group(const TodoItem *it)
{
    return it->done ? 2 : it->has_due ? 0 : 1;
}

void todo_sort_by_due(TodoList *l)
{
    // Insertion sort: stable, and the list is at most TODO_MAX items.
    for (int i = 1; i < l->count; i++)
    {
        TodoItem x = l->items[i];
        int j = i - 1;
        while (j >= 0)
        {
            const TodoItem *y = &l->items[j];
            int gx = sort_group(&x), gy = sort_group(y);
            bool before = gx < gy || (gx == 0 && gy == 0 && date_cmp(x.due, y->due) < 0);
            if (!before)
                break;
            l->items[j + 1] = l->items[j];
            j--;
        }
        l->items[j + 1] = x;
    }
}

// Removes the last word of t when it is a due: or every: token it can read.
static bool take_token(char *t, TodoItem *it)
{
    size_t l = strlen(t);
    size_t start = l;
    while (start > 0 && t[start - 1] != ' ')
        start--;
    char *tok = t + start;
    if (strncmp(tok, "due:", 4) == 0 && !it->has_due)
    {
        Date d;
        if (!date_parse(tok + 4, &d))
            return false;
        it->has_due = true;
        it->due = d;
    }
    else if (strncmp(tok, "every:", 6) == 0 && it->repeat == 0)
    {
        if (!todo_parse_every(tok + 6, &it->repeat, &it->every))
            return false;
    }
    else
    {
        return false;
    }
    *tok = '\0';
    text_trim(t);
    return true;
}

void todo_set_text(TodoItem *it, const char *text)
{
    char tmp[TODO_TEXT_MAX * 2 + 1];
    snprintf(tmp, sizeof(tmp), "%s", text);
    char *t = text_trim(tmp);

    it->has_due = false;
    it->repeat = 0;
    it->every = 1;
    // "due:" and "every:" at the end, in either order.
    for (int k = 0; k < 2 && take_token(t, it); k++)
        ;

    // Keep the text on one line and inside the limit.
    text_copy_clean(it->text, t, TODO_TEXT_MAX, false);
}

bool todo_edit_text(TodoItem *it, const char *text)
{
    TodoItem edited = *it;
    todo_set_text(&edited, text);
    if (edited.text[0] == '\0')
        return false;
    if (!edited.has_due && it->has_due)
    {
        edited.has_due = true;
        edited.due = it->due;
    }
    if (edited.repeat == 0)
    {
        edited.repeat = it->repeat;
        edited.every = it->every;
    }
    *it = edited;
    return true;
}

int todo_expand_due(char *text, size_t size, Date today)
{
    size_t l = strlen(text);
    while (l > 0 && (text[l - 1] == ' ' || text[l - 1] == '\t'))
        text[--l] = '\0';
    // The due: token is the last word, or the one before a final every:.
    size_t end = l, start = l;
    for (int k = 0; k < 2; k++)
    {
        start = end;
        while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '\t')
            start--;
        if (strncasecmp(text + start, "due:", 4) == 0)
            break;
        if (k == 1 || strncasecmp(text + start, "every:", 6) != 0 || start == 0)
            return 0;
        end = start - 1;   // skip the space before every:
        while (end > 0 && text[end - 1] == ' ')
            end--;
    }
    char word[16];
    size_t wl = end - start - 4;
    if (wl >= sizeof(word))
        return -1;
    memcpy(word, text + start + 4, wl);
    word[wl] = '\0';
    Date d;
    if (!date_parse_relative(word, today, &d))
        return -1;
    char iso[11];
    date_format(d, iso);
    // Replace text[start, end) with "due:YYYY-MM-DD", keeping what follows.
    size_t new_len = 4 + 10, tail = l - end;
    if (start + new_len + tail + 1 > size)
        return -1;
    memmove(text + start + new_len, text + end, tail + 1);
    memcpy(text + start, "due:", 4);
    memcpy(text + start + 4, iso, 10);
    return 1;
}

static void parse_line(char *line, void *ctx)
{
    TodoList *l = ctx;
    char *s = text_trim(line);
    if (*s == '\0' || *s == '#')
        return;
    if (l->count >= TODO_MAX)
        return;

    TodoItem *it = &l->items[l->count];
    it->done = false;
    // s[1] first: on a lone "[" at the end of the file, s[2] is past the
    // buffer.
    if (s[0] == '[' && (s[1] == ' ' || s[1] == 'x' || s[1] == 'X') && s[2] == ']')
    {
        it->done = (s[1] != ' ');
        s += 3;
    }
    todo_set_text(it, s);
    if (it->text[0] == '\0' && !it->has_due)
        return; // "[ ]" with nothing after it.
    l->count++;
}

void todo_parse(TodoList *l, char *text)
{
    todo_clear(l);
    text_each_line(text, parse_line, l);
}

bool todo_serialize(const TodoList *l, TextBuf *b)
{
    textbuf_append(b, "# PDA to-do list. [ ] open, [x] done, optional due:YYYY-MM-DD"
                      " and every:Nd/w/m/y\n");
    for (int i = 0; i < l->count; i++)
    {
        const TodoItem *it = &l->items[i];
        textbuf_appendf(b, "[%c] %s", it->done ? 'x' : ' ', it->text);
        const char *sep = it->text[0] ? " " : "";
        if (it->has_due)
        {
            char d[11];
            date_format(it->due, d);
            textbuf_appendf(b, "%sdue:%s", sep, d);
            sep = " ";
        }
        if (it->repeat)
        {
            if (it->every > 1)
                textbuf_appendf(b, "%severy:%d%c", sep, it->every, it->repeat);
            else
                textbuf_appendf(b, "%severy:%c", sep, it->repeat);
        }
        textbuf_append(b, "\n");
    }
    return !b->failed;
}

int todo_add(TodoList *l, const char *text)
{
    if (l->count >= TODO_MAX)
        return -1;
    TodoItem *it = &l->items[l->count];
    it->done = false;
    todo_set_text(it, text);
    if (it->text[0] == '\0' && !it->has_due)
        return -1;
    return l->count++;
}

void todo_remove(TodoList *l, int index)
{
    if (index < 0 || index >= l->count)
        return;
    memmove(&l->items[index], &l->items[index + 1],
            sizeof(TodoItem) * (size_t)(l->count - index - 1));
    l->count--;
}

int todo_move(TodoList *l, int index, int dir)
{
    int other = index + dir;
    if (index < 0 || index >= l->count || other < 0 || other >= l->count)
        return index;
    TodoItem tmp = l->items[index];
    l->items[index] = l->items[other];
    l->items[other] = tmp;
    return other;
}

int todo_open_count(const TodoList *l)
{
    int n = 0;
    for (int i = 0; i < l->count; i++)
        n += !l->items[i].done;
    return n;
}
