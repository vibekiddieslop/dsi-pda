// Habit tracker. Each habit has a goal: every day, or N times a week.
//
// Bottom screen: one row per habit with the last 7 days as boxes; tap a box
// to tick that day. Tap a name to pick the habit; the bar acts on it.
// Top screen: today's progress and the picked habit's streak and history.
#include <stdio.h>

#include <nds.h>

#include "app.h"
#include "screens.h"
#include "sfx.h"
#include "ui/editor.h"
#include "ui/widgets.h"

#define DAYS_SHOWN 7
#define BOX_X 82       // first day box
#define BOX_STEP 20
#define LIST_TOP 37    // below the day letters

static List list;
static Bar bar;
static int day_col = DAYS_SHOWN - 1;       // 6 = today (the D-pad's day)
static int stamp_habit = -1, stamp_col = 0, stamp_frames = 0;

static Date col_day(int col)
{
    return date_add_days(today_date(), col - (DAYS_SHOWN - 1));
}

static bool any(void)
{
    return g_habits.count > 0;
}

static void save(void)
{
    if (!app_save_habits())
        ui_toast("%s", app_last_error());
    list.count = g_habits.count;
    list_fix(&list);
    ui_dirty();
}

// ---------------------------------------------------------------- top

static void draw_top(Surface *s)
{
    Date today = today_date();
    char buf[64];
    gfx_header(s, "Habits", NULL);
    gfx_paper(s, PAPER_Y);

    if (!any())
    {
        gfx_icon(s, 112, 42, ICON_HABITS, 2);
        gfx_text_center(s, 136, PAPER_TEXT(6), "No habits.", C_INK);
        gfx_text_center(s, 136, PAPER_TEXT(7), "Tap Add. Ideas:", C_INK_SOFT);
        gfx_text_center(s, 136, PAPER_TEXT(8), "stretch, read, gym...", C_INK_SOFT);
        return;
    }
    list.count = g_habits.count;
    list_fix(&list);

    // Today's progress.
    int done = habit_done_count(&g_habits, today);
    bool all = done == g_habits.count;
    int x = gfx_chip(s, 20, PAPER_Y, "TODAY", SEC_MID(SEC_HABITS), C_INK);
    if (all)
        snprintf(buf, sizeof(buf), "all %d done!", done);
    else
        snprintf(buf, sizeof(buf), "%d of %d done", done, g_habits.count);
    gfx_text(s, x + 5, PAPER_TEXT(0), buf, all ? C_OK : C_INK);
    int by = PAPER_Y + PAPER_ROW + 1;
    gfx_panel(s, 20, by, 216, 9, 2, C_WHITE, C_INK_SOFT);
    if (done > 0)
        gfx_round_fill(s, 22, by + 2, 212 * done / g_habits.count, 5, 1, SEC_MID(SEC_HABITS));

    // The picked habit.
    const Habit *h = &g_habits.h[list.sel];
    int streak = habit_streak(h, today), best = habit_best_streak(h, today);
    int row = title_draw(s, h->name, 2, C_INK) + 1;
    if (h->goal == 0 && streak >= 7)
        gfx_icon(s, SCREEN_W - 24, PAPER_Y + 2 * PAPER_ROW + 2, ICON_HABITS, 1);   // a gold star

    char goal[24];
    habit_goal_text(h->goal, goal, sizeof(goal));
    x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "GOAL", SEC_MID(SEC_TODO), C_INK);
    gfx_text(s, x + 5, PAPER_TEXT(row), goal, C_INK);
    row++;
    x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "STREAK", SEC_MID(SEC_HABITS), C_INK);
    const char *unit = h->goal > 0 ? "week" : "day";
    snprintf(buf, sizeof(buf), "%d %s%s, best %d", streak, unit, streak == 1 ? "" : "s", best);
    gfx_text_n(s, x + 5, PAPER_TEXT(row), buf, 20, streak > 0 ? C_INK : C_INK_SOFT);
    row++;
    if (h->goal > 0)
    {
        x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "WEEK", SEC_MID(SEC_TIMERS), C_SNOW);
        int wk = habit_week_count(h, today);
        snprintf(buf, sizeof(buf), "%d of %d this week", wk, h->goal);
        gfx_text(s, x + 5, PAPER_TEXT(row), buf, wk >= h->goal ? C_OK : C_INK);
        row++;
    }

    // The last 30 days as a strip, today at the right.
    row++;
    int last = habit_done_last(h, today, 30);
    snprintf(buf, sizeof(buf), "Last 30 days: %d done", last);
    gfx_text(s, 20, PAPER_TEXT(row), buf, C_INK_SOFT);
    row++;
    for (int d = 0; d < 30; d++)
    {
        Date day = date_add_days(today, d - 29);
        int sx = 20 + d * 7, sy = PAPER_Y + row * PAPER_ROW + 2;
        gfx_fill(s, sx, sy, 6, 7, habit_done(h, day) ? C_OK : C_SHADOW);
    }

    static const char *const KEYS[] = { "A", "Check", "X", "Add", "Y", "Menu", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

// ---------------------------------------------------------------- bottom

static void draw_row(Surface *s, int i, int y, bool selected)
{
    const Habit *h = &g_habits.h[i];
    Date today = today_date();
    bool sat = habit_satisfied(h, today);
    gfx_text_n(s, 6, y + 6, h->name, 9, sat ? C_INK_SOFT : C_INK);
    for (int c = 0; c < DAYS_SHOWN; c++)
    {
        int x = BOX_X + c * BOX_STEP;
        gfx_check_box(s, x + 1, y + 2);
        if (habit_done(h, col_day(c)))
        {
            bool landing = i == stamp_habit && c == stamp_col && stamp_frames > 0;
            gfx_stamp_check(s, x + 8, y + 9, landing ? 7 + stamp_frames * 2 : 6, C_OK);
        }
        if (selected && c == day_col && ui_pad_mode())
            gfx_frame(s, x - 1, y, 18, 18, C_ACCENT);   // the D-pad's day
    }
    char buf[12];
    if (h->goal > 0)
    {
        int wk = habit_week_count(h, today);
        snprintf(buf, sizeof(buf), "%d/%d", wk, h->goal);
        gfx_text(s, 224, y + 6, buf, wk >= h->goal ? C_OK : C_INK);
    }
    else
    {
        int streak = habit_streak(h, today);
        if (streak > 0)
        {
            snprintf(buf, sizeof(buf), "%dd", streak > 99 ? 99 : streak);
            gfx_text(s, 224, y + 6, buf, streak >= 7 ? C_OK : C_INK);
        }
    }
}

static void draw_bottom(Surface *s)
{
    list.count = g_habits.count;
    list_draw(&list, s, draw_row);
    // Day letters over the boxes, today's in color.
    gfx_fill(s, 0, UI_HEAD_H, SCREEN_W, LIST_TOP - UI_HEAD_H, C_PAPER_SH);
    gfx_hline(s, 0, LIST_TOP - 1, SCREEN_W, C_RULE);
    static const char *WD1 = "SMTWTFS";
    for (int c = 0; c < DAYS_SHOWN && any(); c++)
    {
        char l[2] = { WD1[date_weekday(col_day(c))], 0 };
        bool now = c == DAYS_SHOWN - 1;
        int x = BOX_X + c * BOX_STEP;
        if (now)
            gfx_round_fill(s, x + 2, UI_HEAD_H + 1, 13, 11, 3, SEC_MID(SEC_HABITS));
        gfx_text(s, x + 5, UI_HEAD_H + 3, l, now ? C_INK : C_INK_SOFT);
    }
    if (!any())
    {
        gfx_text_center(s, SCREEN_W / 2, 80, "No habits.", C_INK_SOFT);
        gfx_text_center(s, SCREEN_W / 2, 96, "Tap Add.", C_INK_SOFT);
    }
    bar.n = 4;
    bar.item[0] = (BarItem){ "Goal", ICON_NONE, SEC_PLAIN, any() };
    bar.item[1] = (BarItem){ "Month", ICON_NONE, SEC_PLAIN, any() };
    bar.item[2] = (BarItem){ "Rename", ICON_NONE, SEC_PLAIN, any() };
    bar.item[3] = (BarItem){ "Add", ICON_PLUS, SEC_HABITS, true };
    bar_draw(s, &bar);
}

// ---------------------------------------------------------------- actions

static void toggle(int i, int col)
{
    if (i < 0 || i >= g_habits.count)
        return;
    Habit *h = &g_habits.h[i];
    Date d = col_day(col);
    bool now_done = !habit_done(h, d);
    habit_set(h, d, now_done);
    save();
    if (!now_done)
    {
        sfx_play(SFX_UNCHECK);
        return;
    }
    sfx_play(SFX_STAMP);
    stamp_habit = i;
    stamp_col = col;
    stamp_frames = 6;
    Date t = today_date();
    int streak = habit_streak(h, t);
    if (h->goal > 0 && habit_week_count(h, t) == h->goal)
        ui_toast("%.14s: weekly goal met!", h->name);
    else if (h->goal == 0 && (streak == 7 || streak == 30 || streak == 100))
        ui_toast("%.14s: %d days in a row! Gold star!", h->name, streak);
    else if (h->goal == 0 && streak > 1)
        ui_toast("%.14s: %d days in a row", h->name, streak);
}

static void add_done(bool ok, const char *text)
{
    if (!ok)
        return;
    int i = habit_add(&g_habits, text, today_date());
    if (i < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast(g_habits.count >= HABIT_MAX ? "Max 16 habits." : "Nothing typed.");
        return;
    }
    list.count = g_habits.count;
    list_show(&list, i);
    sfx_play(SFX_SAVED);
    ui_toast("Added. Now set a goal.");
    save();
}

static void rename_done(bool ok, const char *text)
{
    if (!ok || list.sel >= g_habits.count || text[0] == '\0')
        return;
    habit_rename(&g_habits.h[list.sel], text);
    sfx_play(SFX_SAVED);
    save();
}

static void do_add(void)
{
    editor_open("NEW HABIT", "Name? Like: Stretch", "", HABIT_NAME_MAX, add_done);
}

static void do_rename(void)
{
    if (any())
        editor_open("RENAME HABIT", "New name?", g_habits.h[list.sel].name, HABIT_NAME_MAX,
                    rename_done);
}

static void do_goal(void)
{
    if (!any())
        return;
    Habit *h = &g_habits.h[list.sel];
    h->goal = habit_next_goal(h->goal);
    char goal[24];
    habit_goal_text(h->goal, goal, sizeof(goal));
    ui_toast("Goal: %s", goal);
    save();
}

static HabitList undo_habits;   // the list before a delete, for Undo

static void habit_undo(void)
{
    g_habits = undo_habits;
    save();
}

static void delete_yes(void)
{
    if (list.sel >= g_habits.count)
        return;
    undo_habits = g_habits;
    habit_remove(&g_habits, list.sel);
    sfx_play(SFX_DELETE);
    save();
    ui_undo_offer(habit_undo);
}

static void do_delete(void)
{
    if (any())
        ui_confirm("Delete habit?", g_habits.h[list.sel].name, "Delete", delete_yes);
}

static void do_history(void)
{
    if (any())
        habit_history_open(list.sel);
}

static void act(int a)
{
    switch (a)
    {
        case 0: do_goal(); break;
        case 1: do_history(); break;
        case 2: do_rename(); break;
        case 3: do_delete(); break;
    }
}

static void do_more(void)
{
    if (!any())
    {
        ui_toast("Nothing here. Tap Add.");
        return;
    }
    static const SheetItem items[4] = {
        { "Goal", ICON_CHECK, SEC_PLAIN },
        { "Month view", ICON_CAL, SEC_PLAIN },
        { "Rename", ICON_EDIT, SEC_PLAIN },
        { "Delete", ICON_TRASH, SEC_ALERT },
    };
    ui_sheet(g_habits.h[list.sel].name, items, 4, act);
}

static void update(const Input *in)
{
    if (stamp_frames > 0)
    {
        stamp_frames--;
        ui_dirty_bottom();
    }
    list.count = g_habits.count;
    int b = bar_update(&bar, in);
    if (b >= 0)
    {
        if (b == 3)
            do_add();   // the bar's last key; Delete is in the header
        else
            act(b);
        return;
    }
    ListEvent ev = list_update(&list, in);
    if (ev.kind == LE_SELECT || ev.kind == LE_OPEN)
    {
        if (in->touch_up && in->tx >= BOX_X - 2 && in->tx < BOX_X + DAYS_SHOWN * BOX_STEP)
            toggle(ev.index, (in->tx - (BOX_X - 2)) / BOX_STEP);   // a tap on a day box
        else if (in->touch_up && ev.kind == LE_OPEN)
            do_history();                                           // the picked name again
        else if (in->down & KEY_A)
            toggle(ev.index, day_col);
    }
    // The D-pad's day: left and right, to fix yesterday.
    if ((in->repeat & KEY_LEFT) && day_col > 0)
    {
        day_col--;
        ui_dirty_bottom();
    }
    if ((in->repeat & KEY_RIGHT) && day_col < DAYS_SHOWN - 1)
    {
        day_col++;
        ui_dirty_bottom();
    }
}

static void enter(void)
{
    if (list.height == 0)
        list_init(&list, LIST_TOP, UI_BAR_Y - LIST_TOP, 0);
    list.count = g_habits.count;
    list_fix(&list);
}

static void leave(void)
{
    day_col = DAYS_SHOWN - 1;   // back to today next time
}

const Screen SCR_HABITS = {
    .enter = enter,
    .leave = leave,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title = "Habits",
    .icon = ICON_HABITS,
    .sec = SEC_HABITS,
    .on_add = do_add,
    .on_more = do_more,
    .on_delete = do_delete,
    .can_delete = any,
};
