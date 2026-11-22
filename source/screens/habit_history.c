// Habit month view: one habit's month as a calendar on the bottom screen,
// done days stamped. Tap any day up to today to tick or untick it. The bar
// changes month; the menu steps to the other habits. Top screen: its stats.
#include <stdio.h>

#include <nds.h>

#include "app.h"
#include "screens.h"
#include "sfx.h"
#include "ui/widgets.h"

#define GRID_X 2
#define WEEK_Y 26
#define GRID_Y 37
#define CELL_W 36
#define CELL_H 21

static int habit = 0;
static Date cur;   // the day under the cursor; its month is shown
static Bar bar;
static bool grid_touch = false;

static const Habit *shown(void)
{
    return habit < g_habits.count ? &g_habits.h[habit] : NULL;
}

static void draw_top(Surface *s)
{
    const Habit *h = shown();
    if (h == NULL)
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %d", DATE_MONTH[cur.m - 1], cur.y);
    gfx_header(s, "Habit", buf);
    gfx_paper(s, PAPER_Y);
    Date today = today_date();

    int row = title_draw(s, h->name, 0, C_INK) + 1;
    char goal[24];
    habit_goal_text(h->goal, goal, sizeof(goal));
    int x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "GOAL", SEC_MID(SEC_TODO), C_INK);
    gfx_text(s, x + 5, PAPER_TEXT(row), goal, C_INK);
    row++;
    int streak = habit_streak(h, today), best = habit_best_streak(h, today);
    const char *unit = h->goal > 0 ? "week" : "day";
    x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "STREAK", SEC_MID(SEC_HABITS), C_INK);
    snprintf(buf, sizeof(buf), "%d %s%s, best %d", streak, unit, streak == 1 ? "" : "s", best);
    gfx_text_n(s, x + 5, PAPER_TEXT(row), buf, 20, C_INK);
    row++;

    int dim = date_days_in_month(cur.y, cur.m), month_done = 0;
    for (int d = 1; d <= dim; d++)
        month_done += habit_done(h, (Date){ cur.y, cur.m, d });
    x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "MONTH", SEC_MID(SEC_CAL), C_SNOW);
    snprintf(buf, sizeof(buf), "%d day%s done", month_done, month_done == 1 ? "" : "s");
    gfx_text(s, x + 5, PAPER_TEXT(row), buf, C_INK);
    row++;
    int last = habit_done_last(h, today, 30);
    x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "30 DAYS", SEC_MID(SEC_TIMERS), C_SNOW);
    snprintf(buf, sizeof(buf), "%d done (%d%%)", last, last * 100 / 30);
    gfx_text(s, x + 5, PAPER_TEXT(row), buf, C_INK);

    gfx_text(s, 20, PAPER_TEXT(11), "Tap a day to fix it.", C_INK_SOFT);
    static const char *const KEYS[] = { "A", "Check day", "Y", "Menu", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

static int lead_cells(void)
{
    Date first = { cur.y, cur.m, 1 };
    return (date_weekday(first) + 6) % 7;   // Monday-first column
}

static int day_at(int tx, int ty)
{
    if (tx < GRID_X || ty < GRID_Y)
        return 0;
    int col = (tx - GRID_X) / CELL_W, row = (ty - GRID_Y) / CELL_H;
    if (col > 6 || row > 5)
        return 0;
    int d = row * 7 + col - lead_cells() + 1;
    return d >= 1 && d <= date_days_in_month(cur.y, cur.m) ? d : 0;
}

static void draw_bottom(Surface *s)
{
    const Habit *h = shown();
    gfx_page(s, UI_HEAD_H);
    if (h == NULL)
        return;
    for (int c = 0; c < 7; c++)
        gfx_text_center(s, GRID_X + c * CELL_W + CELL_W / 2, WEEK_Y, DATE_GRID_DAY[c],
                        c >= 5 ? C_WEEKEND : C_INK_SOFT);
    int lead = lead_cells();
    int dim = date_days_in_month(cur.y, cur.m);
    Date today = today_date();
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
        Date day = { cur.y, cur.m, d };
        bool done = habit_done(h, day), future = date_cmp(day, today) > 0;
        if (done)
            gfx_round_fill(s, x + 2, y + 2, CELL_W - 3, CELL_H - 3, 3, SEC_LT(SEC_TODO));
        if (d == cur.d && ui_pad_mode())
            gfx_frame(s, x + 1, y + 1, CELL_W - 1, CELL_H - 1, C_ACCENT);
        else if (date_cmp(day, today) == 0)
            gfx_frame(s, x + 1, y + 1, CELL_W - 1, CELL_H - 1, C_ALERT);
        char num[4];
        snprintf(num, sizeof(num), "%d", d);
        gfx_text(s, x + 4, y + 4, num, future ? C_DIM : (cell % 7) >= 5 ? C_WEEKEND : C_INK);
        if (done)
            gfx_stamp_check(s, x + 25, y + 11, 5, C_OK);
    }
    bar.n = 3;
    bar.item[0] = (BarItem){ NULL, ICON_BACK, SEC_PLAIN, true };
    bar.item[1] = (BarItem){ "Today", ICON_NONE, SEC_PLAIN, true };
    bar.item[2] = (BarItem){ NULL, ICON_NEXT, SEC_PLAIN, true };
    bar_draw(s, &bar);
}

static void move_days(int n)
{
    cur = date_add_days(cur, n);
    ui_dirty();
}

static void move_months(int n)
{
    cur = date_add_months(cur, n);
    ui_dirty();
}

static void toggle_day(void)
{
    if (habit >= g_habits.count)
        return;
    if (date_cmp(cur, today_date()) > 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("That day is not here yet.");
        return;
    }
    Habit *h = &g_habits.h[habit];
    bool done = !habit_done(h, cur);
    habit_set(h, cur, done);
    sfx_play(done ? SFX_STAMP : SFX_UNCHECK);
    if (!app_save_habits())
        ui_toast("%s", app_last_error());
    ui_dirty();
}

static void other_habit(int d)
{
    if (g_habits.count == 0)
        return;
    habit = (habit + d + g_habits.count) % g_habits.count;
    ui_dirty();
}

static void more_done(int i)
{
    other_habit(i == 0 ? +1 : -1);
}

static void do_more(void)
{
    static const SheetItem items[2] = {
        { "Next habit", ICON_NEXT, SEC_PLAIN },
        { "Previous habit", ICON_BACK, SEC_PLAIN },
    };
    ui_sheet("Month view", items, 2, more_done);
}

static void update(const Input *in)
{
    switch (bar_update(&bar, in))
    {
        case 0: move_months(-1); return;
        case 1: cur = today_date(); ui_dirty(); return;
        case 2: move_months(+1); return;
    }
    if (in->touch_down)
    {
        grid_touch = in->ty >= GRID_Y && in->ty < UI_BAR_Y;
    }
    else if (grid_touch && in->touch_up)
    {
        grid_touch = false;
        int d = day_at(in->tx, in->ty);
        if (d > 0)
        {
            cur.d = d;
            toggle_day();
        }
    }
    else if (grid_touch && !in->touching)
    {
        grid_touch = false;
    }

    if (in->repeat & KEY_LEFT)
        move_days(-1);
    if (in->repeat & KEY_RIGHT)
        move_days(+1);
    if (in->repeat & KEY_UP)
        move_days(-7);
    if (in->repeat & KEY_DOWN)
        move_days(+7);
    if (in->repeat & KEY_L)
        move_months(-1);
    if (in->repeat & KEY_R)
        move_months(+1);
    if (in->down & KEY_A)
        toggle_day();
}

static const char *title(void)
{
    static char t[20];
    const Habit *h = shown();
    snprintf(t, sizeof(t), "%.12s", h ? h->name : "Habit");
    return t;
}

static const Screen history_screen = {
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title_fn = title,
    .icon = ICON_HABITS,
    .sec = SEC_HABITS,
    .on_more = do_more,
};

void habit_history_open(int index)
{
    habit = index;
    cur = today_date();
    ui_push(&history_screen);
}
