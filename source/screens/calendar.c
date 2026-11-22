// Month view. Bottom screen: the month grid; tap a day to pick it, tap it
// again to open its list, swipe sideways to change month. Top screen: the
// picked day as a planner page.
//
// Grid dots: blue for events, red when a birthday or anniversary is among
// them, brown for a work shift, green for to-dos due that day.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <nds.h>

#include "app.h"
#include "device.h"
#include "screens.h"
#include "store/work.h"
#include "ui/widgets.h"

#define GRID_X 2
#define WEEK_Y 26       // weekday letters
#define GRID_Y 37
#define CELL_W 36
#define CELL_H 21
#define DAY_MAX 24
#define PAGE_ROWS 9     // entries listed on the top screen

static Date selday;
static bool started = false;
static int day_idx[DAY_MAX];   // events of the picked day
static int day_ev = 0;
static int day_td[DAY_MAX];    // to-dos due that day
static int day_tn = 0;
static const char *day_hol[2];  // US holidays that day
static int day_hn = 0;
static Bar bar;
static bool grid_touch = false, was_picked = false;
static int touch_x0 = 0;

// Dots for the shown month, worked out once per month or change rather
// than on every redraw (every D-pad step).
static uint8_t dots[32];
static uint32_t todo_days;   // bit d-1: a to-do is due on day d
static uint32_t hol_days;    // bit d-1: a holiday
static int dots_y = -1, dots_m = -1;
static bool dots_valid = false;

static void dots_compute(int y, int m)
{
    int dim = date_days_in_month(y, m);
    for (int d = 1; d <= dim; d++)
    {
        Date dd = { y, m, d };
        dots[d] = 0;
        for (int i = 0; i < g_cal.count; i++)
        {
            if (!cal_occurs_on(&g_cal.ev[i], dd))
                continue;
            // Red birthday > brown work > blue event.
            if (cal_is_person(&g_cal.ev[i]))
            {
                dots[d] = C_ALERT;
                break;
            }
            if (cal_is_work(&g_cal.ev[i]))
                dots[d] = C_BROWN;
            else if (dots[d] == 0)
                dots[d] = C_ACCENT;
        }
    }
    Date first = { y, m, 1 };
    todo_days = 0;
    hol_days = 0;
    for (int d = 1; d <= dim; d++)
    {
        const char *h;
        if (day_holidays((Date){ y, m, d }, &h, 1) > 0)
            hol_days |= 1u << (d - 1);
    }
    for (int i = 0; i < g_todo.count; i++)
        if (!g_todo.items[i].done)
            todo_days |= todo_days_mask(&g_todo.items[i], first, dim);
    dots_y = y;
    dots_m = m;
    dots_valid = true;
}

static void refresh_day(void)
{
    day_hn = day_holidays(selday, day_hol, 2);
    day_ev = cal_events_on(&g_cal, selday, day_idx, DAY_MAX);
    day_tn = 0;
    for (int i = 0; i < g_todo.count && day_tn < DAY_MAX; i++)
        if (todo_days_mask(&g_todo.items[i], selday, 1) & 1)
            day_td[day_tn++] = i;
    ui_dirty();
}

// Entries changed (here or on a screen above this one).
static void refresh(void)
{
    dots_valid = false;
    refresh_day();
}

static void enter(void)
{
    if (!started)
    {
        selday = today_date();
        started = true;
    }
    caledit_on_change(refresh);
    refresh();
}

void calendar_show_day(Date d)
{
    selday = d;
    started = true;
}

// ---------------------------------------------------------------- top

static void draw_top(Surface *s)
{
    char buf[CAL_TEXT_MAX + 48];
    snprintf(buf, sizeof(buf), "%s %d", DATE_MONTH[selday.m - 1], selday.y);
    gfx_header(s, "Calendar", buf);
    gfx_paper(s, PAPER_Y);

    // Masthead: the picked day.
    Date now = today_date();
    gfx_fill(s, 17, PAPER_Y, SCREEN_W - 17, 4 * PAPER_ROW - 1, C_PAPER);
    date_block_draw(s, 22, 21, selday);
    gfx_text2x(s, 74, 23, DATE_DAY[date_weekday(selday)], C_INK);
    long days = date_to_days(selday) - date_to_days(now);
    if (days == 0)
        snprintf(buf, sizeof(buf), "Today");
    else if (days == 1)
        snprintf(buf, sizeof(buf), "Tomorrow");
    else if (days == -1)
        snprintf(buf, sizeof(buf), "Yesterday");
    else if (days > 0)
        snprintf(buf, sizeof(buf), "In %ld days", days);
    else
        snprintf(buf, sizeof(buf), "%ld days ago", -days);
    gfx_text(s, 74, 43, buf, days == 0 ? C_ALERT : C_ACCENT);
    int total = day_ev + day_tn;
    if (total == 0)
        snprintf(buf, sizeof(buf), "No plans.");
    else if (total == 1)
        snprintf(buf, sizeof(buf), "1 plan.");
    else
        snprintf(buf, sizeof(buf), "%d plans.", total);
    gfx_text(s, 74, 54, buf, C_INK_SOFT);

    // The day's entries: holidays, events, then to-dos.
    int row = 4;
    if (total == 0 && day_hn == 0)
        gfx_text(s, 20, PAPER_TEXT(row + 1), "Free day. Tap + to add.", C_INK_SOFT);
    for (int h = 0; h < day_hn; h++, row++)   // holidays first, in purple
        gfx_text(s, 20, PAPER_TEXT(row), day_hol[h], C_PURPLE);
    int rows = PAGE_ROWS - day_hn;
    int shown = total > rows ? rows - 1 : total;
    for (int k = 0; k < shown; k++, row++)
    {
        int y = PAPER_TEXT(row);
        if (k < day_ev)
        {
            const CalEvent *e = &g_cal.ev[day_idx[k]];
            char tm[16], label[CAL_TEXT_MAX + 32];
            cal_time_text(e, selday, tm, sizeof(tm));
            cal_label(e, selday, label, sizeof(label));
            int x = gfx_text(s, 20, y, tm, C_ACCENT) + 8;
            if (x < 68)
                x = 68;
            gfx_text_n(s, x, y, label, (SCREEN_W - 6 - x) / FONT_W, event_ink(e));
        }
        else
        {
            const TodoItem *it = &g_todo.items[day_td[k - day_ev]];
            bool late = !it->done && date_cmp(selday, now) < 0;
            gfx_panel(s, 20, PAPER_Y + row * PAPER_ROW + 1, 10, 10, 1, C_WHITE,
                      late ? C_ALERT : C_INK_SOFT);
            if (it->done)
                gfx_stamp_check(s, 25, y + 3, 5, C_OK);
            int end = gfx_text_n(s, 35, y, it->text, 27,
                                 it->done ? C_INK_SOFT : late ? C_ALERT : C_INK);
            if (it->done)
                gfx_hline(s, 35, y + 4, end - 35, C_INK_SOFT);
        }
    }
    if (shown < total)
    {
        snprintf(buf, sizeof(buf), "+%d more. Open day.", total - shown);
        gfx_text(s, 20, PAPER_TEXT(row), buf, C_INK_SOFT);
    }

    static const char *const KEYS[] = { "A", "Open", "X", "Add", "Y", "Menu", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

// ---------------------------------------------------------------- bottom

static int lead_cells(void)
{
    Date first = { selday.y, selday.m, 1 };
    return (date_weekday(first) + 6) % 7;   // Monday-first column
}

// The day of the month under (tx, ty), or 0.
static int day_at(int tx, int ty)
{
    if (tx < GRID_X || ty < GRID_Y)
        return 0;
    int col = (tx - GRID_X) / CELL_W, row = (ty - GRID_Y) / CELL_H;
    if (col > 6 || row > 5)
        return 0;
    int d = row * 7 + col - lead_cells() + 1;
    return d >= 1 && d <= date_days_in_month(selday.y, selday.m) ? d : 0;
}

// ---------------------------------------------------------------- week

// The week view: Monday to Sunday as rows, each a lane from 6a to 10p.
// Timed events and shifts are bars on it with their names; two that overlap
// share the lane, one above the other. An all-day event fills the lane when
// the day has nothing timed, and tints it behind the bars otherwise. Marks
// by the date: red a birthday or anniversary, purple a holiday, green a
// to-do due. A red hand on today's lane shows the time now.
static bool week_mode = false;

#define WK_ROW_H 18
#define TL_X0 56
#define TL_X1 250
#define TL_FROM (6 * 60)
#define TL_TO (22 * 60)
#define WK_BARS 8          // timed bars drawn for one day

static Date week_monday(void)
{
    return date_add_days(selday, -((date_weekday(selday) + 6) % 7));
}

static int tl_x(int minute)
{
    if (minute < TL_FROM)
        minute = TL_FROM;
    if (minute > TL_TO)
        minute = TL_TO;
    return TL_X0 + (minute - TL_FROM) * (TL_X1 - TL_X0) / (TL_TO - TL_FROM);
}

// The week row under ty (0 = Monday), or -1.
static int week_row_at(int ty)
{
    if (ty < GRID_Y || ty >= GRID_Y + 7 * WK_ROW_H)
        return -1;
    return (ty - GRID_Y) / WK_ROW_H;
}

static bool todo_due_on(Date d)
{
    for (int i = 0; i < g_todo.count; i++)
        if (!g_todo.items[i].done && (todo_days_mask(&g_todo.items[i], d, 1) & 1))
            return true;
    return false;
}

// An entry's name without the "Work: " in front of a shift.
static const char *short_name(const CalEvent *e, Date d, char *buf, int size)
{
    cal_label(e, d, buf, size);
    size_t skip = strlen(WORK_PREFIX);
    return cal_is_work(e) && strncmp(buf, WORK_PREFIX, skip) == 0 ? buf + skip : buf;
}

// A bar on a lane: rounded, with a lit top edge, and as much of the name
// as fits when the bar is tall enough for letters.
static void week_bar(Surface *s, int x, int y, int w, int h, uint8_t c, const char *name)
{
    gfx_round_fill(s, x, y, w, h, 2, c);
    if (w > 4 && h >= 8)
        gfx_hline(s, x + 2, y + 1, w - 4, C_RULE);   // a glint, like the stickers
    int chars = (w - 5) / FONT_W;
    if (h >= 10 && chars >= 2 && name != NULL)
    {
        char cut[32];
        snprintf(cut, sizeof(cut), "%.*s", chars > 30 ? 30 : chars, name);
        gfx_text(s, x + 3, y + (h - 7) / 2, cut, C_PAPER);
    }
}

static void week_draw(Surface *s)
{
    static const int HOURS[4] = { 9, 12, 15, 18 };
    // The hours across the top, each on a little tick.
    gfx_fill(s, 1, UI_HEAD_H, SCREEN_W - 2, GRID_Y - UI_HEAD_H - 1, C_PAPER_SH);
    gfx_hline(s, 0, GRID_Y - 1, SCREEN_W, C_RULE);
    for (int h = 0; h < 4; h++)
    {
        char lbl[8];
        time_fmt12(HOURS[h] * 60, TIME_SHORT, lbl, sizeof(lbl));
        gfx_text_center(s, tl_x(HOURS[h] * 60) + 1, WEEK_Y, lbl, C_INK_SOFT);
        gfx_vline(s, tl_x(HOURS[h] * 60), GRID_Y - 3, 2, C_INK_SOFT);
    }

    Date mon = week_monday(), now = today_date();
    time_t t = ui_now();
    struct tm tmv;
    localtime_r(&t, &tmv);
    int now_min = tmv.tm_hour * 60 + tmv.tm_min;

    for (int i = 0; i < 7; i++)
    {
        Date d = date_add_days(mon, i);
        int y = GRID_Y + i * WK_ROW_H;
        bool picked = date_cmp(d, selday) == 0, is_today = date_cmp(d, now) == 0;
        if (picked)
            gfx_round_fill(s, 1, y, SCREEN_W - 2, WK_ROW_H - 1, 3, C_SEL);

        // The date, on a tab: red for today.
        char lb[8];
        snprintf(lb, sizeof(lb), "%s%3d", DATE_GRID_DAY[i], d.d);
        uint8_t ink = i >= 5 ? C_WEEKEND : C_INK;
        if (is_today)
        {
            gfx_round_fill(s, 2, y + 2, 43, 13, 3, SEC_DK(SEC_CAL));
            gfx_round_fill(s, 2, y + 2, 43, 12, 3, SEC_MID(SEC_CAL));
            ink = C_SNOW;
        }
        gfx_text(s, 4, y + 5, lb, ink);

        // The lane: a strip of card with the hour lines on it.
        int ly = y + 2, lh = WK_ROW_H - 5;
        gfx_panel(s, TL_X0 - 3, ly, TL_X1 - TL_X0 + 6, lh, 3, C_WHITE, C_RULE);
        for (int h = 0; h < 4; h++)
            for (int yy = ly + 2; yy < ly + lh - 1; yy += 2)
                gfx_fill(s, tl_x(HOURS[h] * 60), yy, 1, 1, C_RULE);

        // What the day holds: marks by the date, and the bars.
        struct {
            short xa, xb;
            uint8_t c, lane;
            const CalEvent *e;
        } seg[WK_BARS];
        int nb = 0, whole_n = 0;
        const CalEvent *whole = NULL;
        bool person = false;
        int idx[24];
        int n = cal_events_on(&g_cal, d, idx, 24);
        for (int k = 0; k < n; k++)
        {
            const CalEvent *e = &g_cal.ev[idx[k]];
            if (cal_is_person(e))
            {
                person = true;
                continue;
            }
            int span = cal_span(e), di = span ? cal_day_index(e, d) : 0;
            // All day, or a middle day of a timed event over several days.
            if (e->minute == CAL_ALL_DAY || (span > 0 && di > 0 && di < span))
            {
                if (whole == NULL)
                    whole = e;
                whole_n++;
                continue;
            }
            if (nb >= WK_BARS)
                continue;
            int a = (span > 0 && di > 0) ? TL_FROM : e->minute;   // last day: from morning
            int b;
            if (span > 0 && di < span)
                b = TL_TO;                                         // first day: to evening
            else
                b = e->end_minute != CAL_NO_END ? e->end_minute : a + 60;
            if (b <= a)
                b = a + 30;                                        // past midnight
            int xa = tl_x(a), xb = tl_x(b);
            if (xb - xa < 4)
                xb = xa + 4;
            seg[nb].xa = (short)xa;
            seg[nb].xb = (short)xb;
            seg[nb].c = cal_is_work(e) ? C_BROWN : C_ACCENT;
            seg[nb].lane = 0;
            seg[nb].e = e;
            nb++;
        }
        const char *hn;
        if (person)
            gfx_round_fill(s, 47, y + 1, 5, 5, 2, C_ALERT);
        if (day_holidays(d, &hn, 1) > 0)
            gfx_round_fill(s, 47, y + 6, 5, 5, 2, C_PURPLE);
        if (todo_due_on(d))
            gfx_round_fill(s, 47, y + 11, 5, 5, 2, C_OK);

        // Bars that overlap go one above the other (they come in by time).
        int end[2] = { 0, 0 };
        bool two = false;
        for (int k = 0; k < nb; k++)
        {
            int lane = seg[k].xa >= end[0] ? 0 : 1;
            if (lane == 1)
                two = true;
            seg[k].lane = (uint8_t)lane;
            if (seg[k].xb > end[lane])
                end[lane] = seg[k].xb;
        }

        char label[CAL_TEXT_MAX + 32];
        int top = ly + 1, room = lh - 2;   // inside the lane's edge
        if (whole != NULL && nb == 0)
        {
            // Nothing timed: the all-day entry gets the whole lane.
            uint8_t c = cal_is_work(whole) ? C_BROWN : C_ACCENT;
            const char *name = short_name(whole, d, label, sizeof(label));
            week_bar(s, TL_X0 - 2, top, TL_X1 - TL_X0 + 4, room, c, name);
            if (whole_n > 1)
            {
                char more[16];
                snprintf(more, sizeof(more), "+%d", whole_n - 1);
                gfx_text(s, TL_X1 - (int)strlen(more) * FONT_W, top + 2, more, C_PAPER);
            }
        }
        else if (whole != NULL)
        {
            // Behind the timed bars it is a tint over the whole lane.
            gfx_round_fill(s, TL_X0 - 2, top, TL_X1 - TL_X0 + 4, room, 2,
                           cal_is_work(whole) ? SEC_LT(SEC_HABITS) : SEC_LT(SEC_TIMERS));
            for (int h = 0; h < 4; h++)
                for (int yy = ly + 2; yy < ly + lh - 1; yy += 2)
                    gfx_fill(s, tl_x(HOURS[h] * 60), yy, 1, 1, C_RULE);
        }
        for (int k = 0; k < nb; k++)
        {
            int h = two ? (room - 1) / 2 : room;
            int by = top + (two && seg[k].lane ? h + 1 : 0);
            const char *name = short_name(seg[k].e, d, label, sizeof(label));
            week_bar(s, seg[k].xa, by, seg[k].xb - seg[k].xa, h, seg[k].c, name);
        }

        // The time now, on today's lane.
        if (is_today && now_min >= TL_FROM && now_min <= TL_TO)
        {
            int nx = tl_x(now_min);
            gfx_vline(s, nx, ly, lh, C_ALERT);
            gfx_fill(s, nx - 1, ly - 1, 3, 2, C_ALERT);
        }
    }
}

static void month_draw(Surface *s)
{
    for (int c = 0; c < 7; c++)
        gfx_text_center(s, GRID_X + c * CELL_W + CELL_W / 2, WEEK_Y, DATE_GRID_DAY[c],
                        c >= 5 ? C_WEEKEND : C_INK_SOFT);

    if (!dots_valid || dots_y != selday.y || dots_m != selday.m)
        dots_compute(selday.y, selday.m);
    int lead = lead_cells();
    int dim = date_days_in_month(selday.y, selday.m);
    Date now = today_date();
    bool this_month = now.y == selday.y && now.m == selday.m;

    for (int cell = 0; cell < 42; cell++)
    {
        int d = cell - lead + 1;
        int x = GRID_X + (cell % 7) * CELL_W;
        int y = GRID_Y + (cell / 7) * CELL_H;
        gfx_frame(s, x, y, CELL_W + 1, CELL_H + 1, C_RULE);
        if (d < 1 || d > dim)
        {
            gfx_fill(s, x + 1, y + 1, CELL_W - 1, CELL_H - 1, C_PAPER_SH);
            continue;
        }
        if (d == selday.d)
            gfx_round_fill(s, x + 2, y + 2, CELL_W - 3, CELL_H - 3, 3, C_SEL);
        char num[4];
        snprintf(num, sizeof(num), "%d", d);
        uint8_t ink = (cell % 7) >= 5 ? C_WEEKEND : C_INK;
        if (this_month && d == now.d)
        {
            // Today wears a red badge.
            gfx_round_fill(s, x + 2, y + 2, (int)strlen(num) * FONT_W + 5, 12, 3,
                           SEC_MID(SEC_CAL));
            ink = C_SNOW;
        }
        gfx_text(s, x + 5, y + 4, num, ink);
        if (dots[d])
            gfx_round_fill(s, x + 5, y + 15, 6, 4, 1, dots[d]);
        if (hol_days & (1u << (d - 1)))
            gfx_round_fill(s, x + 21, y + 15, 6, 4, 1, C_PURPLE);
        if (todo_days & (1u << (d - 1)))
            gfx_round_fill(s, x + 13, y + 15, 6, 4, 1, C_OK);
    }

}

static void draw_bottom(Surface *s)
{
    gfx_page(s, UI_HEAD_H);
    if (week_mode)
        week_draw(s);
    else
        month_draw(s);
    // [<] [Week|Month] [Agenda] [>]: the arrows page by month, or by week in
    // the week view. Today is in the menu.
    bar.n = 4;
    bar.narrow = 0x09;   // the two arrows
    bar.item[0] = (BarItem){ NULL, ICON_BACK, SEC_PLAIN, true };
    bar.item[1] = (BarItem){ week_mode ? "Month" : "Week", ICON_NONE, SEC_CAL, true };
    bar.item[2] = (BarItem){ "Agenda", ICON_NONE, SEC_PLAIN, true };
    bar.item[3] = (BarItem){ NULL, ICON_NEXT, SEC_PLAIN, true };
    bar_draw(s, &bar);
}

// ---------------------------------------------------------------- input

static void move_days(int n)
{
    selday = date_add_days(selday, n);
    refresh_day();
}

static void move_months(int n)
{
    selday = date_add_months(selday, n);
    refresh_day();
}

static void go_today(void)
{
    selday = today_date();
    refresh_day();
}

static void open_day(void)
{
    agenda_open(selday, 1);
}

static void open_agenda(void)
{
    agenda_open(today_date(), AGENDA_DAYS);
}

static void do_add(void)
{
    caledit_add(selday);
}

static void toggle_week(void)
{
    week_mode = !week_mode;
    ui_dirty();
}

// A page back or on: a month, or a week in the week view.
static void page(int dir)
{
    if (week_mode)
        move_days(7 * dir);
    else
        move_months(dir);
}

static void more_done(int i)
{
    switch (i)
    {
        case 0: open_day(); break;
        case 1: go_today(); break;
        case 2: toggle_week(); break;
        case 3: open_agenda(); break;
    }
}

static void do_more(void)
{
    SheetItem items[4] = {
        { "Open day", ICON_LIST, SEC_PLAIN },
        { "Today", ICON_CAL, SEC_PLAIN },
        { week_mode ? "Month view" : "Week view", ICON_CAL_BLANK, SEC_PLAIN },
        { "Agenda (30 days)", ICON_PAGE, SEC_PLAIN },
    };
    ui_sheet("Calendar", items, 4, more_done);
}

// The day under the stylus: a cell of the month, or a row of the week.
// Returns false when there is none.
static bool touched_day(int tx, int ty, Date *out)
{
    if (week_mode)
    {
        int r = week_row_at(ty);
        if (r < 0)
            return false;
        *out = date_add_days(week_monday(), r);
        return true;
    }
    int d = day_at(tx, ty);
    if (d == 0)
        return false;
    *out = (Date){ selday.y, selday.m, d };
    return true;
}

static void update(const Input *in)
{
    switch (bar_update(&bar, in))
    {
        case 0: page(-1); return;
        case 1: toggle_week(); return;
        case 2: open_agenda(); return;
        case 3: page(+1); return;
    }

    if (in->touch_down)
    {
        grid_touch = in->ty >= GRID_Y && in->ty < UI_BAR_Y;
        touch_x0 = in->tx;
        Date d;
        bool on_day = grid_touch && touched_day(in->tx, in->ty, &d);
        was_picked = on_day && date_cmp(d, selday) == 0;
        if (on_day && !was_picked)
        {
            device_click();
            selday = d;
            refresh_day();
        }
    }
    else if (grid_touch && in->touch_up)
    {
        grid_touch = false;
        int dx = in->tx - touch_x0;
        Date d;
        if (dx > 60)
            page(-1);              // the page follows the stylus
        else if (dx < -60)
            page(+1);
        else if (was_picked && touched_day(in->tx, in->ty, &d) && date_cmp(d, selday) == 0)
            open_day();
    }
    else if (grid_touch && !in->touching)
    {
        grid_touch = false;
    }

    // The month grid: left/right a day, up/down a week. The week's rows:
    // up/down a day, left/right a week.
    int lr = week_mode ? 7 : 1, ud = week_mode ? 1 : 7;
    if (in->repeat & KEY_LEFT)
        move_days(-lr);
    if (in->repeat & KEY_RIGHT)
        move_days(+lr);
    if (in->repeat & KEY_UP)
        move_days(-ud);
    if (in->repeat & KEY_DOWN)
        move_days(+ud);
    if (in->repeat & KEY_L)
        page(-1);
    if (in->repeat & KEY_R)
        page(+1);
    if (in->down & KEY_A)
        open_day();
}

static const char *title(void)
{
    static char t[24];
    if (week_mode)
    {
        // "Nov 23-29", or "Nov 30-Dec 6" across a month's end.
        Date a = week_monday(), b = date_add_days(a, 6);
        if (a.m == b.m)
            snprintf(t, sizeof(t), "%.3s %d-%d", DATE_MONTH[a.m - 1], a.d, b.d);
        else
            snprintf(t, sizeof(t), "%.3s %d-%.3s %d", DATE_MONTH[a.m - 1], a.d,
                     DATE_MONTH[b.m - 1], b.d);
        return t;
    }
    snprintf(t, sizeof(t), "%s %d", DATE_MONTH[selday.m - 1], selday.y);
    if (strlen(t) > 13)
        snprintf(t, sizeof(t), "%.3s %d", DATE_MONTH[selday.m - 1], selday.y);
    return t;
}

const Screen SCR_CALENDAR = {
    .enter = enter,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title_fn = title,
    .icon = ICON_CAL,
    .sec = SEC_CAL,
    .on_add = do_add,
    .on_more = do_more,
};
