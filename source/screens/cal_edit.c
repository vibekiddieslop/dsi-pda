// Adding and changing calendar entries: events, birthdays, work shifts and
// the to-dos due on a day. Shared by the month view and the day list and
// agenda, which ask to be told after each change (caledit_on_change).
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <nds.h>

#include "app.h"
#include "screens.h"
#include "sfx.h"
#include "store/work.h"
#include "ui/editor.h"

static void (*on_change)(void);
static Date add_day;          // the day new entries go on
static int cur_event = -1;    // event being edited, timed or deleted
static int cur_todo = -1;

void caledit_on_change(void (*cb)(void))
{
    on_change = cb;
}

static void changed(void)
{
    if (on_change != NULL)
        on_change();
    ui_dirty();
}

static void save_cal(void)
{
    if (!app_save_cal())
        ui_toast("%s", app_last_error());
    changed();
}

static void save_todo(void)
{
    if (!app_save_todo())
        ui_toast("%s", app_last_error());
    changed();
}

static void day_text(Date d, char *out, int size)
{
    snprintf(out, (size_t)size, "%s %d %s", DATE_DAY3[date_weekday(d)], d.d, DATE_MONTH3[d.m - 1]);
}

// ---------------------------------------------------------------- time

static void time_done(const EvTime *t)
{
    if (cur_event < 0 || cur_event >= g_cal.count)
        return;
    CalEvent *e = &g_cal.ev[cur_event];
    e->minute = t->all_day ? CAL_ALL_DAY : t->start;
    e->end_minute = t->all_day ? CAL_NO_END : t->end;
    e->end = date_add_days(e->start, t->days);
    sfx_play(SFX_SAVED);
    save_cal();
}

// The usual start for a new time: the next whole hour today, else 9:00.
static int default_start(Date day)
{
    if (date_cmp(day, today_date()) != 0)
        return 9 * 60;
    time_t now = ui_now();
    struct tm tm;
    localtime_r(&now, &tm);
    return tm.tm_hour < 23 ? (tm.tm_hour + 1) * 60 : 23 * 60;
}

// fresh: a new event without a typed time starts as a one-hour slot.
static void open_time(int i, bool fresh)
{
    const CalEvent *e = &g_cal.ev[i];
    EvTime t;
    t.days = cal_span(e);
    if (e->minute != CAL_ALL_DAY)
    {
        t.all_day = false;
        t.start = e->minute;
        t.end = e->end_minute;
    }
    else
    {
        t.all_day = !fresh;
        t.start = default_start(e->start);
        t.end = t.start + 60 < 1440 ? t.start + 60 : CAL_NO_END;
    }
    cur_event = i;
    evtime_open(e->text, e->start, &t, time_done);
}

void caledit_event_time(int i)
{
    if (i < 0 || i >= g_cal.count)
        return;
    if (cal_is_person(&g_cal.ev[i]))
    {
        ui_toast("Birthdays are all day.");
        return;
    }
    open_time(i, false);
}

// ---------------------------------------------------------------- adding

static void event_done(bool ok, const char *text)
{
    if (!ok)
        return;
    int minute, end_minute;
    char t[CAL_TEXT_MAX + 1];
    if (!cal_parse_entry(text, &minute, &end_minute, t, sizeof(t)))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Nothing typed.");
        return;
    }
    int i = cal_add(&g_cal, add_day, minute, '-', t);
    if (i < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Calendar full.");
        return;
    }
    g_cal.ev[i].end_minute = end_minute;
    sfx_play(SFX_SAVED);
    save_cal();
    // A time typed with the text ("2pm Dentist") is all it needs. Without
    // one, the time card asks once.
    if (minute == CAL_ALL_DAY)
    {
        open_time(i, true);
    }
    else
    {
        char clock[12];
        time_fmt12(minute, TIME_MIN, clock, sizeof(clock));
        ui_toast("Added at %s", clock);
    }
}

static void add_event(void)
{
    char title[24], day[16];
    day_text(add_day, day, sizeof(day));
    snprintf(title, sizeof(title), "EVENT %s", day);
    editor_open(title, "Event? Like: 2pm Lunch", "", CAL_TEXT_MAX + 12, event_done);
}

static void todo_added(bool ok, const char *typed)
{
    if (!ok)
        return;
    // "due:fri" and friends work here too, as on the To-Do screen.
    char text[TODO_TEXT_MAX + 40];
    todo_expand_typed(typed, text, sizeof(text));
    int i = todo_add(&g_todo, text);
    if (i < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast(g_todo.count >= TODO_MAX ? "List full."
                                          : "Nothing typed.");
        return;
    }
    TodoItem *it = &g_todo.items[i];
    if (!it->has_due)
    {
        it->has_due = true;   // due on the day it was added on
        it->due = add_day;
    }
    char day[16];
    day_text(it->due, day, sizeof(day));
    ui_toast("Due %s", day);
    sfx_play(SFX_SAVED);
    save_todo();
}

static void add_todo(void)
{
    char title[24], day[16];
    day_text(add_day, day, sizeof(day));
    snprintf(title, sizeof(title), "TO-DO %s", day);
    editor_open(title, "To-do text?", "", TODO_TEXT_MAX + 15, todo_added);
}

// "Sam 1996" on the day becomes a birthday (the year is optional).
static void bday_done(bool ok, const char *text)
{
    if (!ok)
        return;
    char name[CAL_TEXT_MAX + 1];
    int year;
    if (!cal_parse_person(text, name, sizeof(name), &year))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Need a name. Like: Sam 1996");
        return;
    }
    Date start = { year, add_day.m, add_day.d };
    if (!date_valid(start))
    {
        sfx_play(SFX_ERROR);
        ui_toast("%04d has no %02d-%02d", year, add_day.m, add_day.d);
        return;
    }
    if (cal_add(&g_cal, start, CAL_ALL_DAY, 'B', name) < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Calendar full.");
        return;
    }
    sfx_play(SFX_SAVED);
    save_cal();
    ui_toast("Birthday added.");
}

static void add_bday(void)
{
    char title[24], day[16];
    day_text(add_day, day, sizeof(day));
    snprintf(title, sizeof(title), "BIRTHDAY %.10s", day);
    editor_open(title, "Name, year. Like: Sam 1996", "", CAL_TEXT_MAX, bday_done);
}

// A shift picked for add_day. One shift per day: a shift already on that
// day is replaced. The custom time slot's hours are chosen next.
static void shift_done(int shift)
{
    char text[CAL_TEXT_MAX + 1];
    bool custom = shift < 0 || shift >= g_shifts.count;
    snprintf(text, sizeof(text), WORK_PREFIX "%s", custom ? SHIFT_CUSTOM_NAME : g_shifts.s[shift].name);
    int i = -1;
    for (int k = 0; k < g_cal.count; k++)
    {
        const CalEvent *e = &g_cal.ev[k];
        if (cal_is_work(e) && e->repeat == '-' && date_cmp(e->start, add_day) == 0)
        {
            i = k;
            break;
        }
    }
    bool replaced = i >= 0;
    if (!replaced && (i = cal_add(&g_cal, add_day, CAL_ALL_DAY, '-', text)) < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Calendar full.");
        return;
    }
    CalEvent *e = &g_cal.ev[i];
    snprintf(e->text, sizeof(e->text), "%s", text);
    e->end = e->start;
    int s, en;
    sfx_play(SFX_SAVED);
    if (!custom)
    {
        shift_hours(&g_shifts.s[shift], add_day, &s, &en);
        e->minute = s;
        e->end_minute = en;
        char a[12], b[12];
        time_fmt12(s, TIME_SHORT, a, sizeof(a));
        time_fmt12(en, TIME_SHORT, b, sizeof(b));
        ui_toast("%s %s-%s", replaced ? "Shift changed:" : "Work", a, b);
        save_cal();
    }
    else
    {
        e->minute = CAL_ALL_DAY;
        e->end_minute = CAL_NO_END;
        save_cal();
        open_time(i, true);   // custom time slot: pick the hours
    }
}

static void add_chosen(int i)
{
    switch (i)
    {
        case 0: add_event(); break;
        case 1: add_todo(); break;
        case 2: add_bday(); break;
        case 3: shift_open(add_day, shift_done); break;
    }
}

void caledit_add(Date day)
{
    static const SheetItem items[4] = {
        { "Event", ICON_CAL, SEC_PLAIN },
        { "To-do", ICON_TODO, SEC_PLAIN },
        { "Birthday or anniversary", ICON_CAKE, SEC_PLAIN },
        { "Work shift", ICON_WORK, SEC_PLAIN },
    };
    static char title[28];
    char text[16];
    add_day = day;
    day_text(day, text, sizeof(text));
    snprintf(title, sizeof(title), "Add to %s", text);
    ui_sheet(title, items, 4, add_chosen);
}

// ---------------------------------------------------------------- events

static void edit_event_done(bool ok, const char *text)
{
    if (!ok || cur_event < 0 || cur_event >= g_cal.count)
        return;
    CalEvent *e = &g_cal.ev[cur_event];
    if (cal_is_person(e))
    {
        char name[CAL_TEXT_MAX + 1];
        int year;
        if (!cal_parse_person(text, name, sizeof(name), &year))
        {
            sfx_play(SFX_ERROR);
            ui_toast("Need a name. Like: Sam 1996");
            return;
        }
        Date start = { year, e->start.m, e->start.d };
        if (!date_valid(start))
        {
            sfx_play(SFX_ERROR);
            ui_toast("%04d has no %02d-%02d", year, e->start.m, e->start.d);
            return;
        }
        e->start = e->end = start;
        snprintf(e->text, sizeof(e->text), "%s", name);
    }
    else
    {
        int minute, end_minute;
        char t[CAL_TEXT_MAX + 1];
        if (!cal_parse_entry(text, &minute, &end_minute, t, sizeof(t)))
        {
            sfx_play(SFX_ERROR);
            ui_toast("No text. Use the trash.");
            return;
        }
        if (minute != CAL_ALL_DAY)   // a time typed here replaces the old one
        {
            e->minute = minute;
            e->end_minute = end_minute;
        }
        snprintf(e->text, sizeof(e->text), "%s", t);
    }
    sfx_play(SFX_SAVED);
    save_cal();
}

void caledit_event_edit(int i)
{
    if (i < 0 || i >= g_cal.count)
        return;
    const CalEvent *e = &g_cal.ev[i];
    if (cal_is_work(e) && e->repeat == '-')
    {
        add_day = e->start;
        shift_open(e->start, shift_done);   // change the shift
        return;
    }
    char init[CAL_TEXT_MAX + 12];
    const char *prompt = "Edit the text.";
    if (cal_is_person(e))
    {
        if (e->start.y != CAL_YEAR_UNKNOWN)
            snprintf(init, sizeof(init), "%s %04d", e->text, e->start.y);
        else
            snprintf(init, sizeof(init), "%s", e->text);
        prompt = "Name, year. Like: Sam 1996";
    }
    else
    {
        snprintf(init, sizeof(init), "%s", e->text);
    }
    cur_event = i;
    editor_open("EDIT EVENT", prompt, init, CAL_TEXT_MAX + 6, edit_event_done);
}

void caledit_event_repeat(int i)
{
    if (i < 0 || i >= g_cal.count)
        return;
    CalEvent *e = &g_cal.ev[i];
    e->repeat = cal_next_repeat(e->repeat);
    static const char *names[] = { "once", "every day", "every week", "every month",
                                   "every year", "a birthday", "an anniversary" };
    const char *codes = "-DWMYBA";
    ui_toast(cal_is_person(e) ? "Now %s" : "Repeats: %s", names[strchr(codes, e->repeat) - codes]);
    save_cal();
}

// Undo after a delete: the list as it was, in RAM until the Undo key goes.
static CalList undo_cal;
static TodoList undo_todo;

static void event_undo(void)
{
    g_cal = undo_cal;
    save_cal();
}

static void todo_undo(void)
{
    g_todo = undo_todo;
    save_todo();
}

static void delete_event_yes(void)
{
    if (cur_event < 0 || cur_event >= g_cal.count)
        return;
    undo_cal = g_cal;
    cal_remove(&g_cal, cur_event);
    sfx_play(SFX_DELETE);
    save_cal();
    ui_undo_offer(event_undo);
}

void caledit_event_delete(int i)
{
    if (i < 0 || i >= g_cal.count)
        return;
    const CalEvent *e = &g_cal.ev[i];
    const char *q = e->repeat == 'B'   ? "Delete birthday?"
                    : e->repeat == 'A' ? "Delete anniversary?"
                    : e->repeat != '-' ? "Delete all repeats?"
                                       : "Delete event?";
    cur_event = i;
    ui_confirm(q, e->text, "Delete", delete_event_yes);
}

// ---------------------------------------------------------------- to-dos

bool caledit_todo_check(int i)
{
    if (i < 0 || i >= g_todo.count)
        return false;
    TodoItem *it = &g_todo.items[i];
    bool stamped = false;
    if (it->done)
    {
        it->done = false;
        sfx_play(SFX_UNCHECK);
    }
    else if (todo_complete(it, today_date()))
    {
        char day[16];
        day_text(it->due, day, sizeof(day));
        sfx_play(SFX_STAMP);
        ui_toast("Done! Next: %s", day);
    }
    else
    {
        sfx_play(SFX_STAMP);
        stamped = true;
        if (todo_open_count(&g_todo) == 0)
            ui_toast("All done. Good job!");
    }
    save_todo();
    return stamped;
}

static void todo_when_done(const WhenValue *v)
{
    if (cur_todo < 0 || cur_todo >= g_todo.count)
        return;
    TodoItem *it = &g_todo.items[cur_todo];
    it->has_due = v->has_due;
    it->due = v->due;
    it->repeat = v->repeat;
    it->every = v->every;
    sfx_play(SFX_SAVED);
    if (it->has_due)
    {
        char day[16];
        day_text(it->due, day, sizeof(day));
        ui_toast("Due %s", day);
    }
    save_todo();
}

void caledit_todo_date(int i)
{
    if (i < 0 || i >= g_todo.count)
        return;
    const TodoItem *it = &g_todo.items[i];
    WhenValue v = { it->has_due, it->has_due ? it->due : today_date(), it->repeat, it->every };
    cur_todo = i;
    when_open(it->text, &v, todo_when_done);
}

static void todo_edit_done(bool ok, const char *typed)
{
    if (!ok || cur_todo < 0 || cur_todo >= g_todo.count)
        return;
    char text[TODO_TEXT_MAX + 40];
    todo_expand_typed(typed, text, sizeof(text));
    if (!todo_edit_text(&g_todo.items[cur_todo], text))
    {
        sfx_play(SFX_ERROR);
        ui_toast("No text. Use the trash.");
        return;
    }
    sfx_play(SFX_SAVED);
    save_todo();
}

void caledit_todo_edit(int i)
{
    if (i < 0 || i >= g_todo.count)
        return;
    cur_todo = i;
    editor_open("EDIT TO-DO", "Edit the text.", g_todo.items[i].text, TODO_TEXT_MAX + 15,
                todo_edit_done);
}

static void delete_todo_yes(void)
{
    if (cur_todo < 0 || cur_todo >= g_todo.count)
        return;
    undo_todo = g_todo;
    todo_remove(&g_todo, cur_todo);
    sfx_play(SFX_DELETE);
    save_todo();
    ui_undo_offer(todo_undo);
}

void caledit_todo_delete(int i)
{
    if (i < 0 || i >= g_todo.count)
        return;
    cur_todo = i;
    ui_confirm("Delete to-do?", g_todo.items[i].text, "Delete", delete_todo_yes);
}
