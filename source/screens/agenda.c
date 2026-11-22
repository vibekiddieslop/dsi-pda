// The entries of one day, or the agenda: overdue to-dos, then the next 30
// days under date headers. Bottom screen: the list. Tap a to-do's box to
// check it, tap a row to pick it, tap it again to edit; the bar acts on the
// picked row. Top screen: the picked entry in full.
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "app.h"
#include "screens.h"
#include "store/work.h"
#include "ui/widgets.h"

#define ROWS_MAX 320
#define BOX_W 26

// ROW_HOLIDAY: a US holiday (index = which one that day), read only.
typedef enum { ROW_HEADER, ROW_EVENT, ROW_TODO, ROW_HOLIDAY } RowKind;

typedef struct {
    RowKind kind;
    Date day;      // the day the row belongs to (headers too)
    int index;     // event or to-do index
    bool overdue;  // header "Overdue" / overdue to-do
} Row;

static Row rows[ROWS_MAX];
static int nrows = 0;
static List list;
static Bar bar;
static Date from, today;
static int span = AGENDA_DAYS;     // 1: a single day, without headers
static int stamp_row = -1, stamp_frames = 0;

// Bit d set: to-do i falls on from + d (its due date or a repeat of it).
static uint32_t due_mask[TODO_MAX];

static void add_row(RowKind k, Date day, int index, bool overdue)
{
    if (nrows < ROWS_MAX)
        rows[nrows++] = (Row){ k, day, index, overdue };
}

static void build(void)
{
    nrows = 0;
    today = today_date();
    bool agenda = span > 1;

    if (agenda)
    {
        // Overdue open to-dos.
        bool any = false;
        for (int i = 0; i < g_todo.count; i++)
        {
            const TodoItem *it = &g_todo.items[i];
            if (it->done || !it->has_due || date_cmp(it->due, today) >= 0)
                continue;
            if (!any)
                add_row(ROW_HEADER, today, 0, true);
            any = true;
            add_row(ROW_TODO, it->due, i, true);
        }
    }
    // The agenda leaves out what is checked; a single day keeps it, struck
    // through.
    for (int i = 0; i < g_todo.count; i++)
    {
        const TodoItem *it = &g_todo.items[i];
        due_mask[i] = it->done && agenda ? 0 : todo_days_mask(it, from, span);
    }

    static int idx[CAL_MAX];
    for (int d = 0; d < span; d++)
    {
        Date day = date_add_days(from, d);
        int n = cal_events_on(&g_cal, day, idx, CAL_MAX);
        bool has = !agenda;   // header added for this day (a single day has none)
        const char *hol[2];
        int nh = day_holidays(day, hol, 2);
        for (int h = 0; h < nh; h++)
        {
            if (!has)
                add_row(ROW_HEADER, day, 0, false);
            has = true;
            add_row(ROW_HOLIDAY, day, h, false);
        }
        for (int k = 0; k < n; k++)
        {
            if (!has)
                add_row(ROW_HEADER, day, 0, false);
            has = true;
            add_row(ROW_EVENT, day, idx[k], false);
        }
        for (int i = 0; i < g_todo.count; i++)
        {
            if (!(due_mask[i] & (1u << d)))
                continue;
            if (!has)
                add_row(ROW_HEADER, day, 0, false);
            has = true;
            add_row(ROW_TODO, day, i, false);
        }
    }
    list.count = nrows;
    list_fix(&list);
}

static bool selectable(int i)
{
    return i >= 0 && i < nrows && rows[i].kind != ROW_HEADER && rows[i].kind != ROW_HOLIDAY;
}

// Moves the selection off a header: on in direction dir, else back.
static void skip_headers(int dir)
{
    int i = list.sel;
    while (i >= 0 && i < nrows && !selectable(i))
        i += dir;
    if (!selectable(i))
    {
        i = list.sel;
        while (i >= 0 && i < nrows && !selectable(i))
            i -= dir;
    }
    if (selectable(i) && i != list.sel)
    {
        list_show(&list, i);
        // Keep the day's header in view above its first row.
        if (i > 0 && rows[i - 1].kind == ROW_HEADER && list.scroll > (i - 1) * UI_ROW_H)
            list.scroll = (i - 1) * UI_ROW_H;
        list_fix(&list);
    }
}

static const Row *picked(void)
{
    return selectable(list.sel) ? &rows[list.sel] : NULL;
}

static void refresh(void)
{
    build();
    skip_headers(+1);
    ui_dirty();
}

static void enter(void)
{
    if (list.height == 0)
        list_init(&list, UI_HEAD_H, UI_BAR_Y - UI_HEAD_H, BOX_W);
    caledit_on_change(refresh);
    refresh();
}

void agenda_open(Date first, int days)
{
    from = first;
    span = days < 1 ? 1 : days > 32 ? 32 : days;
    list.sel = list.scroll = 0;
    ui_push(&SCR_AGENDA);
}

// ---------------------------------------------------------------- top

static const char *repeat_words(char r)
{
    switch (r)
    {
        case 'D': return "every day";
        case 'W': return "every week";
        case 'M': return "every month";
        case 'Y': case 'B': case 'A': return "every year";
        default: return "never";
    }
}

static void draw_event_detail(Surface *s, const Row *r)
{
    const CalEvent *e = &g_cal.ev[r->index];
    char buf[64], label[CAL_TEXT_MAX + 32];
    if (e->repeat == 'B')
        gfx_chip(s, 20, PAPER_Y, "BIRTHDAY", SEC_MID(SEC_ALERT), C_SNOW);
    else if (e->repeat == 'A')
        gfx_chip(s, 20, PAPER_Y, "ANNIVERSARY", SEC_MID(SEC_ALERT), C_SNOW);
    else if (cal_is_work(e))
        gfx_chip(s, 20, PAPER_Y, "WORK", SEC_MID(SEC_HABITS), C_INK);
    else
        gfx_chip(s, 20, PAPER_Y, "EVENT", SEC_MID(SEC_TIMERS), C_SNOW);
    cal_label(e, r->day, label, sizeof(label));
    int row = title_draw(s, label, 1, event_ink(e)) + 1;

    int x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "WHEN", SEC_MID(SEC_CAL), C_SNOW);
    snprintf(buf, sizeof(buf), "%s %d %s", DATE_DAY3[date_weekday(r->day)], r->day.d,
             DATE_MONTH3[r->day.m - 1]);
    x = gfx_text(s, x + 5, PAPER_TEXT(row), buf, C_INK);
    char tm[16];
    cal_time_text(e, r->day, tm, sizeof(tm));
    gfx_text(s, x + 8, PAPER_TEXT(row), e->minute == CAL_ALL_DAY ? "all day" : tm, C_ACCENT);
    row++;
    if (cal_span(e) > 0)
    {
        snprintf(buf, sizeof(buf), "Day %d of %d", cal_day_index(e, r->day) + 1, cal_span(e) + 1);
        gfx_text(s, 20, PAPER_TEXT(row), buf, C_INK_SOFT);
        row++;
    }
    x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "REPEAT", SEC_MID(SEC_TIMERS), C_SNOW);
    gfx_text(s, x + 5, PAPER_TEXT(row), repeat_words(e->repeat),
             e->repeat == '-' ? C_INK_SOFT : C_INK);
}

static void draw_top(Surface *s)
{
    char buf[40];
    if (span == 1)
        snprintf(buf, sizeof(buf), "%s %d %s %d", DATE_DAY3[date_weekday(from)], from.d,
                 DATE_MONTH3[from.m - 1], from.y);
    else
        snprintf(buf, sizeof(buf), "next %d days", span);
    gfx_header(s, span == 1 ? "Day" : "Agenda", buf);
    gfx_paper(s, PAPER_Y);

    const Row *r = picked();
    if (r == NULL)
    {
        gfx_icon(s, 112, 54, ICON_SUN, 2);
        gfx_text_center(s, 136, PAPER_TEXT(7), span == 1 ? "Free day." : "Nothing coming up.",
                        C_INK);
        gfx_text_center(s, 136, PAPER_TEXT(8), "Tap Add.", C_INK_SOFT);
        return;
    }
    if (r->kind == ROW_TODO)
        todo_detail_draw(s, &g_todo.items[r->index], today, NULL);
    else
        draw_event_detail(s, r);

    static const char *const TODO_KEYS[] = { "A", "Check", "X", "Add", "Y", "Menu", NULL };
    static const char *const EVENT_KEYS[] = { "A", "Edit", "X", "Add", "Y", "Menu", NULL };
    legend_draw(s, PAPER_TEXT(13), r->kind == ROW_TODO ? TODO_KEYS : EVENT_KEYS);
}

// ---------------------------------------------------------------- bottom

static void draw_row(Surface *s, int i, int y, bool selected)
{
    (void)selected;
    const Row *row = &rows[i];
    char buf[CAL_TEXT_MAX + 48];
    int right = SCREEN_W - 8;
    if (row->kind == ROW_HEADER)
    {
        // A date tab across the margin.
        gfx_fill(s, 0, y, SCREEN_W, UI_ROW_H - 1, C_PAPER_SH);
        if (row->overdue)
        {
            gfx_chip(s, 5, y + 4, "OVERDUE", SEC_MID(SEC_ALERT), C_SNOW);
            return;
        }
        snprintf(buf, sizeof(buf), "%s %d %s", DATE_DAY3[date_weekday(row->day)], row->day.d,
                 DATE_MONTH3[row->day.m - 1]);
        int x = gfx_chip(s, 5, y + 4, buf, SEC_MID(SEC_CAL), C_SNOW);
        long days = date_to_days(row->day) - date_to_days(today);
        if (days == 0)
            gfx_text(s, x + 6, y + 6, "today", C_ALERT);
        else if (days == 1)
            gfx_text(s, x + 6, y + 6, "tomorrow", C_INK_SOFT);
        else if (days < 7)
        {
            snprintf(buf, sizeof(buf), "in %ld days", days);
            gfx_text(s, x + 6, y + 6, buf, C_INK_SOFT);
        }
        return;
    }
    if (row->kind == ROW_HOLIDAY)
    {
        const char *hol[2];
        if (row->index < day_holidays(row->day, hol, 2))
        {
            gfx_round_fill(s, 9, y + 5, 8, 8, 3, C_PURPLE);
            gfx_text_n(s, BOX_W + 6, y + 6, hol[row->index], 27, C_PURPLE);
        }
        return;
    }
    if (row->kind == ROW_EVENT)
    {
        const CalEvent *e = &g_cal.ev[row->index];
        if (e->repeat == 'B')
            gfx_icon(s, 5, y + 1, ICON_CAKE, 1);
        else if (e->repeat == 'A')
            gfx_icon(s, 5, y + 1, ICON_HEART, 1);
        else if (cal_is_work(e))
            gfx_icon(s, 5, y + 1, ICON_WORK, 1);
        else
            gfx_round_fill(s, 9, y + 5, 8, 8, 3, C_ACCENT);
        if (e->repeat == 'D' || e->repeat == 'W' || e->repeat == 'M' || e->repeat == 'Y')
        {
            gfx_icon(s, right - 16, y + 2, ICON_REPEAT, 1);
            right -= 18;
        }
        char tm[16], label[CAL_TEXT_MAX + 32];
        cal_time_text(e, row->day, tm, sizeof(tm));
        cal_label(e, row->day, label, sizeof(label));
        int x = BOX_W + 6;
        if (e->minute != CAL_ALL_DAY)
            x = gfx_text(s, x, y + 6, tm, C_ACCENT) + 6;
        gfx_text_n(s, x, y + 6, label, (right - x) / FONT_W, event_ink(e));
        return;
    }
    const TodoItem *it = &g_todo.items[row->index];
    gfx_check_box(s, 6, y + 2);
    if (it->done)
    {
        int size = i == stamp_row && stamp_frames > 0 ? 7 + stamp_frames * 2 : 7;
        gfx_stamp_check(s, 13, y + 9, size, C_OK);
    }
    if (row->overdue)
    {
        long late = date_to_days(today) - date_to_days(it->due);
        snprintf(buf, sizeof(buf), "%ldd late", late > 99 ? 99 : late);
        int w = (int)strlen(buf) * FONT_W + 7;
        gfx_chip(s, right - w, y + 4, buf, SEC_MID(SEC_ALERT), C_SNOW);
        right -= w + 4;
    }
    if (it->repeat)
    {
        gfx_icon(s, right - 16, y + 2, ICON_REPEAT, 1);
        right -= 18;
    }
    int x = BOX_W + 6;
    int end = gfx_text_n(s, x, y + 6, it->text, (right - x) / FONT_W,
                         it->done ? C_INK_SOFT : C_INK);
    if (it->done)
        gfx_hline(s, x, y + 10, end - x, C_INK_SOFT);
}

static void draw_bottom(Surface *s)
{
    list.count = nrows;
    list_draw(&list, s, draw_row);
    const Row *r = picked();
    if (nrows == 0)
    {
        gfx_text_center(s, SCREEN_W / 2, 70, "Nothing here.", C_INK_SOFT);
        gfx_text_center(s, SCREEN_W / 2, 86, "Tap Add.", C_INK_SOFT);
    }
    // Add is always the last key; Delete is the header's trash.
    bar.n = 1;
    bar.item[0] = (BarItem){ "Add", ICON_PLUS, SEC_CAL, true };
    if (r != NULL && r->kind == ROW_TODO)
    {
        bar.n = 3;
        bar.item[0] = (BarItem){ "Edit", ICON_EDIT, SEC_PLAIN, true };
        bar.item[1] = (BarItem){ "Date", ICON_CAL, SEC_PLAIN, true };
        bar.item[2] = (BarItem){ "Add", ICON_PLUS, SEC_CAL, true };
    }
    else if (r != NULL)
    {
        bool person = cal_is_person(&g_cal.ev[r->index]);
        bar.n = 4;
        bar.item[0] = (BarItem){ "Edit", ICON_NONE, SEC_PLAIN, true };
        bar.item[1] = (BarItem){ "Time", ICON_NONE, SEC_PLAIN, !person };
        bar.item[2] = (BarItem){ person ? "Type" : "Repeat", ICON_NONE, SEC_PLAIN, true };
        bar.item[3] = (BarItem){ "Add", ICON_PLUS, SEC_CAL, true };
    }
    bar_draw(s, &bar);
}

// ---------------------------------------------------------------- actions

// Bar and menu actions share numbers: 0 edit, 1 date or time, 2 delete
// (to-do) or repeat (event), 3 delete (event), 9 show in the calendar.
static void act(int a)
{
    const Row *r = picked();
    if (r == NULL)
        return;
    if (a == 9)
    {
        calendar_show_day(r->day);
        while (ui_current() != &SCR_CALENDAR && ui_depth() > 1)
            ui_pop();
        return;
    }
    if (r->kind == ROW_TODO)
    {
        switch (a)
        {
            case 0: caledit_todo_edit(r->index); break;
            case 1: caledit_todo_date(r->index); break;
            case 2: caledit_todo_delete(r->index); break;
        }
        return;
    }
    switch (a)
    {
        case 0: caledit_event_edit(r->index); break;
        case 1: caledit_event_time(r->index); break;
        case 2: caledit_event_repeat(r->index); break;
        case 3: caledit_event_delete(r->index); break;
    }
}

static void check(int i)
{
    if (!selectable(i) || rows[i].kind != ROW_TODO)
        return;
    int todo = rows[i].index;
    if (caledit_todo_check(todo))   // rebuilds the rows
    {
        // Follow the item for its stamp (a single day keeps checked rows).
        for (int k = 0; k < nrows; k++)
            if (rows[k].kind == ROW_TODO && rows[k].index == todo)
            {
                stamp_row = k;
                stamp_frames = 6;
            }
    }
}

static int menu_map[6];

static void more_done(int i)
{
    act(menu_map[i]);
}

static void do_more(void)
{
    const Row *r = picked();
    if (r == NULL)
    {
        ui_toast("Nothing here. Tap Add.");
        return;
    }
    SheetItem items[6];
    int n = 0;
#define ITEM(text, icn, color, action)                    \
    do {                                                  \
        items[n] = (SheetItem){ text, icn, color };       \
        menu_map[n++] = action;                           \
    } while (0)
    ITEM("Edit", ICON_EDIT, SEC_PLAIN, 0);
    if (r->kind == ROW_TODO)
    {
        ITEM("Date and repeat", ICON_CAL, SEC_PLAIN, 1);
        ITEM("Delete", ICON_TRASH, SEC_ALERT, 2);
    }
    else
    {
        bool person = cal_is_person(&g_cal.ev[r->index]);
        if (!person)
            ITEM("Time", ICON_TIMERS, SEC_PLAIN, 1);
        ITEM(person ? "Birthday or anniversary" : "Repeat", ICON_REPEAT, SEC_PLAIN, 2);
        ITEM("Delete", ICON_TRASH, SEC_ALERT, 3);
    }
    if (span > 1)
        ITEM("Show in calendar", ICON_CAL, SEC_PLAIN, 9);
#undef ITEM
    ui_sheet(span == 1 ? "Entry" : "Agenda", items, n, more_done);
}

// A new entry goes on the picked row's day (today for the overdue part or
// an empty agenda).
static void do_add(void)
{
    const Row *r = picked();
    Date day = span == 1 ? from : today;
    if (r != NULL && !r->overdue)
        day = r->day;
    caledit_add(day);
}

// The header's trash: deletes the picked to-do or event (it asks first).
static bool can_delete(void)
{
    return picked() != NULL;
}

static void do_delete(void)
{
    const Row *r = picked();
    if (r != NULL)
        act(r->kind == ROW_TODO ? 2 : 3);
}

static void update(const Input *in)
{
    if (stamp_frames > 0)
    {
        stamp_frames--;
        ui_dirty_bottom();
    }
    list.count = nrows;
    int b = bar_update(&bar, in);
    if (b >= 0)
    {
        if (b == bar.n - 1)
            do_add();   // Add is the last key
        else
            act(b);
        return;
    }
    int old = list.sel;
    ListEvent ev = list_update(&list, in);
    if (ev.kind == LE_NONE)
        return;
    if (!selectable(list.sel))
    {
        // Landed on a date header: step on to the next entry.
        skip_headers(list.sel >= old ? +1 : -1);
        ui_dirty();
        return;
    }
    const Row *r = picked();
    if (ev.kind == LE_BOX && r->kind == ROW_TODO)
        check(list.sel);
    else if (ev.kind == LE_OPEN || (ev.kind == LE_BOX && (in->down & KEY_A)))
        act(0);
}

static const char *title(void)
{
    static char t[24];
    if (span > 1)
        return "Agenda";
    snprintf(t, sizeof(t), "%s %d %s", DATE_DAY3[date_weekday(from)], from.d,
             DATE_MONTH3[from.m - 1]);
    return t;
}

const Screen SCR_AGENDA = {
    .enter = enter,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title_fn = title,
    .icon = ICON_LIST,
    .sec = SEC_CAL,
    .on_add = do_add,
    .on_more = do_more,
    .on_delete = do_delete,
    .can_delete = can_delete,
};
