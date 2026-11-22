// When: a to-do's due date and repeat.
//
// Bottom screen: a month you tap. Tap the day it is due; the arrows (or a
// swipe) turn the month. Below it, how it comes back: Never, or every Day,
// Week, Month or Year (tap the chosen one again for every 2, 3 or 4). The
// bar has No date, Today and Done. Top screen: the date on a tear-off block
// and the repeat in words.
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "screens.h"
#include "sfx.h"
#include "store/todo_io.h"
#include "ui/widgets.h"

#define NAV_Y 27
#define NAV_H 18
#define WEEK_Y 47        // weekday letters
#define GRID_X 2
#define GRID_Y 57
#define CELL_W 36
#define CELL_H 14
#define REP_Y 143
#define REP_H 19
#define REPS 5           // Never, Day, Week, Month, Year

static WhenValue val;
static WhenDone on_done;
static char text[TODO_TEXT_MAX + 1];
static int view_y, view_m;          // the month shown
static Press nav, reps;
static Bar bar;
static bool grid_touch = false;
static int touch_x0 = 0;

static const char UNITS[4] = { 'd', 'w', 'm', 'y' };

static Date base(void)
{
    return val.has_due ? val.due : today_date();
}

static void set_due(Date d)
{
    val.has_due = true;
    val.due = d;
    view_y = d.y;
    view_m = d.m;
    ui_dirty();
}

static void no_date(void)
{
    val.has_due = false;
    val.repeat = 0;      // a repeat counts from a date
    val.every = 1;
    ui_dirty();
}

static void turn_month(int n)
{
    Date d = date_add_months((Date){ view_y, view_m, 1 }, n);
    view_y = d.y;
    view_m = d.m;
    ui_dirty();
}

static void pick_repeat(char unit)
{
    if (unit == 0)
    {
        val.repeat = 0;
        val.every = 1;
    }
    else if (val.repeat == unit)
    {
        val.every = val.every % 4 + 1;   // tap again: every 2, 3, 4, then 1
    }
    else
    {
        val.repeat = unit;
        val.every = 1;
    }
    if (val.repeat && !val.has_due)
        set_due(today_date());           // a repeat counts from a date
    ui_dirty();
}

static void cycle_repeat(void)
{
    int i = 0;
    while (i < 4 && UNITS[i] != val.repeat)
        i++;
    pick_repeat(val.repeat == 0 ? 'd' : i + 1 < 4 ? UNITS[i + 1] : 0);
}

static void finish(void)
{
    WhenValue v = val;
    WhenDone cb = on_done;
    ui_pop();
    if (cb != NULL)
        cb(&v);
}

// ---------------------------------------------------------------- grid

static int lead_cells(void)
{
    return (date_weekday((Date){ view_y, view_m, 1 }) + 6) % 7;   // Monday-first column
}

// The day of the shown month under (tx, ty), or 0.
static int day_at(int tx, int ty)
{
    if (tx < GRID_X || ty < GRID_Y)
        return 0;
    int col = (tx - GRID_X) / CELL_W, row = (ty - GRID_Y) / CELL_H;
    if (col > 6 || row > 5)
        return 0;
    int d = row * 7 + col - lead_cells() + 1;
    return d >= 1 && d <= date_days_in_month(view_y, view_m) ? d : 0;
}

// Days of the shown month the to-do comes back on (bit d - 1), with the
// repeat as it is set now.
static uint32_t repeat_days(void)
{
    if (!val.has_due || !val.repeat)
        return 0;
    TodoItem it;
    memset(&it, 0, sizeof(it));
    it.has_due = true;
    it.due = val.due;
    it.repeat = val.repeat;
    it.every = val.every;
    return todo_days_mask(&it, (Date){ view_y, view_m, 1 }, date_days_in_month(view_y, view_m));
}

// ---------------------------------------------------------------- input

static void nav_rects(Rect *r)
{
    r[0] = (Rect){ 4, NAV_Y, 34, NAV_H };
    r[1] = (Rect){ SCREEN_W - 38, NAV_Y, 34, NAV_H };
}

static void rep_rects(Rect *r)
{
    for (int i = 0; i < REPS; i++)
        r[i] = (Rect){ (short)(24 + i * 46), REP_Y, 44, REP_H };
}

static void update(const Input *in)
{
    Rect r[REPS];
    nav_rects(r);
    int n = press_update(&nav, in, r, 2);
    if (n >= 0)
        turn_month(n == 0 ? -1 : +1);
    rep_rects(r);
    int k = press_update(&reps, in, r, REPS);
    if (k == 0)
        pick_repeat(0);
    else if (k > 0)
        pick_repeat(UNITS[k - 1]);
    switch (bar_update(&bar, in))
    {
        case 0: no_date(); break;
        case 1: set_due(today_date()); break;
        case 2: finish(); return;
    }

    // The month: a tap picks the day, a swipe turns the page.
    if (in->touch_down)
    {
        grid_touch = in->ty >= GRID_Y && in->ty < GRID_Y + 6 * CELL_H;
        touch_x0 = in->tx;
    }
    else if (grid_touch && in->touch_up)
    {
        grid_touch = false;
        int dx = in->tx - touch_x0, d = day_at(in->tx, in->ty);
        if (dx > 60)
            turn_month(-1);
        else if (dx < -60)
            turn_month(+1);
        else if (d > 0)
        {
            sfx_play(SFX_STAMP);
            set_due((Date){ view_y, view_m, d });
        }
    }
    else if (grid_touch && !in->touching)
    {
        grid_touch = false;
    }

    if (in->repeat & KEY_LEFT)
        set_due(date_add_days(base(), -1));
    if (in->repeat & KEY_RIGHT)
        set_due(date_add_days(base(), 1));
    if (in->repeat & KEY_UP)
        set_due(date_add_days(base(), -7));
    if (in->repeat & KEY_DOWN)
        set_due(date_add_days(base(), 7));
    if (in->repeat & KEY_L)
        set_due(date_add_months(base(), -1));
    if (in->repeat & KEY_R)
        set_due(date_add_months(base(), 1));
    if (in->down & KEY_Y)
        cycle_repeat();
    if (in->down & KEY_X)
        no_date();
    else if (in->down & (KEY_A | KEY_START))
        finish();
}

// ---------------------------------------------------------------- top

static void draw_top(Surface *s)
{
    char buf[64];
    gfx_header(s, "When", NULL);
    gfx_paper(s, PAPER_Y);
    if (strlen(text) <= 14)
        title_draw(s, text, 0, C_INK);
    else
        gfx_text_n(s, 20, PAPER_TEXT(1), text, 29, C_INK);

    // The date on a tear-off block.
    Date t = today_date();
    gfx_fill(s, 17, PAPER_Y + 3 * PAPER_ROW, SCREEN_W - 17, 5 * PAPER_ROW - 1, C_PAPER);
    if (val.has_due)
    {
        long days = date_to_days(val.due) - date_to_days(t);
        date_block_draw(s, 22, 60, val.due);
        gfx_text2x(s, 74, 61, DATE_DAY[date_weekday(val.due)], C_INK);
        if (days == 0)
            snprintf(buf, sizeof(buf), "Today");
        else if (days == 1)
            snprintf(buf, sizeof(buf), "Tomorrow");
        else if (days == -1)
            snprintf(buf, sizeof(buf), "Yesterday: late");
        else if (days > 0)
            snprintf(buf, sizeof(buf), "In %ld days", days);
        else
            snprintf(buf, sizeof(buf), "%ld days ago: late", -days);
        gfx_text(s, 74, 81, buf, days < 0 ? C_ALERT : C_ACCENT);
        snprintf(buf, sizeof(buf), "%s %d, %d", DATE_MONTH[val.due.m - 1], val.due.d, val.due.y);
        gfx_text(s, 74, 92, buf, C_INK_SOFT);
    }
    else
    {
        gfx_round_fill(s, 24, 62, 42, 42, 3, C_SHADOW);
        gfx_panel(s, 22, 60, 42, 42, 3, C_WHITE, C_INK_SOFT);
        gfx_text2x(s, 35, 73, "?", C_INK_SOFT);
        gfx_text2x(s, 74, 61, "No date", C_INK);
        gfx_text(s, 74, 81, "Stays on the list.", C_INK_SOFT);
    }

    // How it comes back.
    int row = 8;
    int x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "REPEAT", SEC_MID(SEC_TIMERS), C_SNOW);
    if (val.repeat)
    {
        todo_repeat_text(val.repeat, val.every, buf, sizeof(buf));
        gfx_text_n(s, x + 5, PAPER_TEXT(row), buf, 21, C_INK);
        Date next = todo_next_due(val.due, val.repeat, val.every);
        snprintf(buf, sizeof(buf), "Then: %s %d %s", DATE_DAY3[date_weekday(next)], next.d,
                 DATE_MONTH3[next.m - 1]);
        gfx_text(s, 20, PAPER_TEXT(row + 1), buf, C_INK_SOFT);
    }
    else
    {
        gfx_text(s, x + 5, PAPER_TEXT(row), "never", C_INK_SOFT);
    }

    static const char *const KEYS[] = { "A", "Done", "X", "No date", "Y", "Repeat", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

// ---------------------------------------------------------------- bottom

static void rep_label(int i, char *out, int size)
{
    static const char *one[REPS] = { "Never", "Day", "Week", "Month", "Year" };
    static const char *many[REPS] = { "", "day", "wk", "mo", "yr" };
    if (i > 0 && val.repeat == UNITS[i - 1] && val.every > 1)
        snprintf(out, (size_t)size, "%d %s", val.every, many[i]);
    else
        snprintf(out, (size_t)size, "%s", one[i]);
}

static void draw_bottom(Surface *s)
{
    char buf[32];
    gfx_page(s, UI_HEAD_H);

    // The month and its arrows.
    Rect r[REPS];
    nav_rects(r);
    gfx_button(s, r[0].x, r[0].y, r[0].w, r[0].h, NULL, ICON_BACK, SEC_PLAIN, press_is(&nav, 0),
               true);
    gfx_button(s, r[1].x, r[1].y, r[1].w, r[1].h, NULL, ICON_NEXT, SEC_PLAIN, press_is(&nav, 1),
               true);
    snprintf(buf, sizeof(buf), "%s %d", DATE_MONTH[view_m - 1], view_y);
    gfx_text_center(s, SCREEN_W / 2, NAV_Y + 5, buf, C_INK);
    for (int c = 0; c < 7; c++)
        gfx_text_center(s, GRID_X + c * CELL_W + CELL_W / 2, WEEK_Y, DATE_GRID_DAY[c],
                        c >= 5 ? C_WEEKEND : C_INK_SOFT);

    int lead = lead_cells(), dim = date_days_in_month(view_y, view_m);
    Date now = today_date();
    uint32_t again = repeat_days();
    for (int cell = 0; cell < 42; cell++)
    {
        int d = cell - lead + 1;
        int x = GRID_X + (cell % 7) * CELL_W, y = GRID_Y + (cell / 7) * CELL_H;
        gfx_frame(s, x, y, CELL_W + 1, CELL_H + 1, C_RULE);
        if (d < 1 || d > dim)
        {
            gfx_fill(s, x + 1, y + 1, CELL_W - 1, CELL_H - 1, C_PAPER_SH);
            continue;
        }
        Date day = { view_y, view_m, d };
        bool due = val.has_due && date_cmp(day, val.due) == 0;
        bool is_today = date_cmp(day, now) == 0, past = date_cmp(day, now) < 0;
        uint8_t ink = past ? C_DIM : (cell % 7) >= 5 ? C_WEEKEND : C_INK;
        if (due)
        {
            // The due day wears a green sticker.
            gfx_panel(s, x + 2, y + 1, CELL_W - 3, CELL_H - 1, 3, SEC_MID(SEC_TODO),
                      SEC_DK(SEC_TODO));
            ink = C_INK;
        }
        else if (is_today)
        {
            gfx_panel(s, x + 2, y + 1, CELL_W - 3, CELL_H - 1, 3, C_PAPER, C_ALERT);
        }
        snprintf(buf, sizeof(buf), "%d", d);
        gfx_text_center(s, x + CELL_W / 2 + 1, y + 4, buf, ink);
        // A small ring mark on the days it comes back.
        if (!due && (again & (1u << (d - 1))))
            gfx_round_fill(s, x + CELL_W - 9, y + 5, 5, 5, 2, SEC_MID(SEC_TIMERS));
    }

    // How it comes back: the chosen key stays down, in color.
    gfx_icon(s, 4, REP_Y + 1, ICON_REPEAT, 1);
    rep_rects(r);
    for (int i = 0; i < REPS; i++)
    {
        bool on = i == 0 ? val.repeat == 0 : val.repeat == UNITS[i - 1];
        rep_label(i, buf, sizeof(buf));
        gfx_button(s, r[i].x, r[i].y, r[i].w, r[i].h, buf, ICON_NONE,
                   on ? SEC_TIMERS : SEC_PLAIN, on || press_is(&reps, i), true);
    }

    bar.n = 3;
    bar.narrow = 0;
    bar.item[0] = (BarItem){ "No date", ICON_NONE, SEC_PLAIN, val.has_due };
    bar.item[1] = (BarItem){ "Today", ICON_NONE, SEC_PLAIN, true };
    bar.item[2] = (BarItem){ "Done", ICON_CHECK, SEC_TODO, true };
    bar_draw(s, &bar);
}

static const Screen when_screen = {
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title = "Due date",
    .icon = ICON_CAL,
    .sec = SEC_TODO,
};

void when_open(const char *todo_text, const WhenValue *init, WhenDone done)
{
    snprintf(text, sizeof(text), "%s", todo_text);
    val = *init;
    if (val.every < 1)
        val.every = 1;
    Date shown = base();
    view_y = shown.y;
    view_m = shown.m;
    grid_touch = false;
    on_done = done;
    ui_push(&when_screen);
}
