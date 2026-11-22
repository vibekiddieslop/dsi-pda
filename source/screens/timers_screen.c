// Countdown timers (RAM only) and daily alarms (ALARMS.TXT).
//
// Bottom screen: New timer and three small keys that start a 1, 5 or 10
// minute timer (or the last one again) in one tap, then the running
// countdowns and the alarms. Tap the icon at the left of a row to pause a
// countdown or switch an alarm on and off. New and edited times go through
// the time picker (picker.c), whose header switches a new timer to an
// alarm. With nothing set, the page opens straight on a new timer.
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "app.h"
#include "screens.h"
#include "sfx.h"
#include "timers.h"
#include "ui/widgets.h"

#define BOX_W 30
#define STRIP_Y 28
#define STRIP_H 28
#define LIST_TOP 60
#define QUICK_N 3
#define STRIP_KEYS (1 + QUICK_N + 1)   // New timer, the quick ones, again

static const int QUICK_MIN[QUICK_N] = { 1, 5, 10 };

static int last_seconds = 5 * 60;   // "Again" repeats the last timer started.
static int editing = -1;            // Alarm index being edited, -1 for a new one.
static List list;
static Bar bar;
static Press strip;

// Rows: active timers first, then alarms.
static int timer_slots[TIMER_MAX];
static int timer_n = 0;

static void collect(void)
{
    timer_n = 0;
    for (int i = 0; i < TIMER_MAX; i++)
        if (g_timers[i].used)
            timer_slots[timer_n++] = i;
    list.count = timer_n + g_alarms.count;
    list_fix(&list);
}

static bool on_timer(void)
{
    return list.sel < timer_n;
}

static bool on_alarm(void)
{
    return list.sel >= timer_n && list.sel - timer_n < g_alarms.count;
}

// ---------------------------------------------------------------- top

static void draw_top(Surface *s)
{
    collect();
    draw_clock_header(s, "Timers");
    gfx_paper(s, PAPER_Y);
    time_t now = ui_now();
    char buf[64];

    // The countdown that ends first, big.
    int best = -1, best_left = 0;
    for (int k = 0; k < timer_n; k++)
    {
        const Countdown *c = &g_timers[timer_slots[k]];
        int left = timers_left(c, now);
        if (c->running && (best < 0 || left < best_left))
        {
            best = k;
            best_left = left;
        }
    }
    gfx_fill(s, 17, PAPER_Y, SCREEN_W - 17, 5 * PAPER_ROW - 1, C_PAPER);
    if (best >= 0)
    {
        snprintf(buf, sizeof(buf), "%d:%02d", best_left / 60, best_left % 60);
        int w = (int)strlen(buf) * 32;
        gfx_text_scaled(s, 136 - w / 2, 26, buf, best_left <= 10 ? C_ALERT : C_INK, 4);
        gfx_text_center(s, 136, 63, timer_n > 1 ? "until next ring" : "until it rings",
                        C_INK_SOFT);
    }
    else
    {
        gfx_icon(s, 120, 24, ICON_TIMERS, 2);
        gfx_text_center(s, 136, 60, timer_n ? "Timers paused." : "No timer running.",
                        C_INK_SOFT);
    }

    int row = 6;
    int x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "NEXT ALARM", SEC_MID(SEC_TIMERS), C_SNOW);
    char alarm[48];
    if (timers_next_alarm(now, alarm, sizeof(alarm)))
        gfx_text_n(s, 20, PAPER_TEXT(row + 1), alarm, 29, C_INK);
    else
        gfx_text(s, x + 5, PAPER_TEXT(row), "none", C_INK_SOFT);
    row += 3;
    gfx_text(s, 20, PAPER_TEXT(row), "Alarms need the lid open.", C_INK_SOFT);

    static const char *const KEYS[] = { "A", "On/off", "X", "New", "Y", "Menu", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

// ---------------------------------------------------------------- bottom

// The strip under the header: [New timer] [1m] [5m] [10m] [again].
static void strip_rects(Rect *r)
{
    r[0] = (Rect){ 4, STRIP_Y, 104, STRIP_H };
    for (int i = 1; i < STRIP_KEYS; i++)
        r[i] = (Rect){ (short)(112 + (i - 1) * 36), STRIP_Y, 32, STRIP_H };
}

static void draw_row(Surface *s, int i, int y, bool selected)
{
    (void)selected;
    char buf[64];
    time_t now = ui_now();
    if (i < timer_n)
    {
        const Countdown *c = &g_timers[timer_slots[i]];
        int left = timers_left(c, now);
        gfx_icon(s, 7, y + 2, c->running ? ICON_PAUSE : ICON_PLAY, 1);
        snprintf(buf, sizeof(buf), "%d:%02d", left / 60, left % 60);
        int x = gfx_text(s, BOX_W + 6, y + 6, buf, c->running ? C_INK : C_INK_SOFT) + 8;
        if (c->label[0])   // a snooze: what it rings for
            snprintf(buf, sizeof(buf), "%s%s", c->label, c->running ? "" : ", paused");
        else if (c->total % 60)
            snprintf(buf, sizeof(buf), "of %d:%02d%s", c->total / 60, c->total % 60,
                     c->running ? "" : ", paused");
        else
            snprintf(buf, sizeof(buf), "of %d min%s", c->total / 60, c->running ? "" : ", paused");
        gfx_text_n(s, x, y + 6, buf, (SCREEN_W - 8 - x) / FONT_W, C_INK_SOFT);
        return;
    }
    const Alarm *a = &g_alarms.a[i - timer_n];
    // A little switch: the knob at the right, green, when the alarm is on.
    gfx_panel(s, 4, y + 4, 22, 11, 4, a->on ? SEC_MID(SEC_TODO) : C_SHADOW, C_INK_SOFT);
    gfx_panel(s, a->on ? 15 : 4, y + 3, 11, 13, 4, C_WHITE, C_INK_SOFT);
    char clock[12];
    time_fmt12(a->minute, TIME_MIN, clock, sizeof(clock));
    uint8_t ink = a->on ? C_INK : C_INK_SOFT;
    int x = gfx_text(s, BOX_W + 6, y + 6, clock, ink) + 8;
    // Days as words when they make a usual set, else the letters.
    const char *days = strcmp(a->days, "MTWTFSS") == 0   ? ""
                       : strcmp(a->days, "MTWTF--") == 0 ? "Weekdays "
                       : strcmp(a->days, "-----SS") == 0 ? "Weekend "
                                                         : NULL;
    if (days == NULL)
        snprintf(buf, sizeof(buf), "%s %s", a->days, a->label);
    else
        snprintf(buf, sizeof(buf), "%s%s", days, a->label);
    gfx_text_n(s, x, y + 6, buf, (SCREEN_W - 8 - x) / FONT_W, ink);
}

static void draw_bottom(Surface *s)
{
    collect();
    list_draw(&list, s, draw_row);
    if (list.count == 0)
        gfx_text_center(s, SCREEN_W / 2, 104, "No timers. No alarms.", C_INK_SOFT);

    // New timer, big, then the small quick keys: one tap starts it.
    gfx_fill(s, 0, UI_HEAD_H, SCREEN_W, LIST_TOP - UI_HEAD_H, C_PAPER_SH);
    gfx_hline(s, 0, LIST_TOP - 1, SCREEN_W, C_RULE);
    Rect r[STRIP_KEYS];
    strip_rects(r);
    gfx_button(s, r[0].x, r[0].y, r[0].w, r[0].h, "New timer", ICON_PLUS, SEC_TIMERS,
               press_is(&strip, 0), true);
    char label[8];
    for (int i = 0; i < QUICK_N; i++)
    {
        snprintf(label, sizeof(label), "%dm", QUICK_MIN[i]);
        gfx_button(s, r[1 + i].x, r[1 + i].y, r[1 + i].w, r[1 + i].h, label, ICON_NONE,
                   SEC_PLAIN, press_is(&strip, 1 + i), true);
    }
    // The last length again, as an icon.
    int k = STRIP_KEYS - 1;
    gfx_button(s, r[k].x, r[k].y, r[k].w, r[k].h, NULL, ICON_REPEAT, SEC_PLAIN,
               press_is(&strip, k), true);

    bar.n = 2;
    if (on_timer())
        bar.item[0] = (BarItem){ g_timers[timer_slots[list.sel]].running ? "Pause" : "Resume",
                                 ICON_NONE, SEC_PLAIN, true };
    else
        bar.item[0] = (BarItem){ "Edit", ICON_EDIT, SEC_PLAIN, on_alarm() };
    bar.item[1] = (BarItem){ "New alarm", ICON_BELL, SEC_TIMERS, true };
    bar_draw(s, &bar);
}

// ---------------------------------------------------------------- actions

static void save_alarms(void)
{
    if (!app_save_alarms())
        ui_toast("%s", app_last_error());
    collect();
    ui_dirty();
}

static void start_seconds(int seconds)
{
    int slot = timers_add(seconds, ui_now());
    if (slot < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Max 4 timers.");
        return;
    }
    last_seconds = seconds;
    collect();
    // Pick the new countdown. It takes the first free slot, which is not
    // always the last row (an earlier timer may have ended).
    for (int k = 0; k < timer_n; k++)
        if (timer_slots[k] == slot)
            list_show(&list, k);
    sfx_play(SFX_TIMER);
    ui_dirty();
}

static void timer_picked(const PickValue *v)
{
    start_seconds(v->a * 60 + v->b);
}

static void alarm_picked(const PickValue *v);

// New timer or new alarm: one page, with a key in its header to switch.
// A timer starts from the last length; an alarm from the next whole hour,
// where the most common alarm times are.
static void open_new(PickMode start)
{
    time_t now = ui_now();
    struct tm t;
    localtime_r(&now, &t);
    PickValue timer = { last_seconds / 60, last_seconds % 60, "", "" };
    PickValue alarm = { (t.tm_hour + 1) % 24, 0, "MTWTFSS", "" };
    editing = -1;
    picker_open_new(start, &timer, &alarm, timer_picked, alarm_picked);
}

static void new_timer(void)
{
    open_new(PICK_TIMER);
}

static void alarm_picked(const PickValue *v)
{
    Alarm a;
    a.minute = v->a * 60 + v->b;
    char clock[12];
    time_fmt12(a.minute, TIME_MIN, clock, sizeof(clock));
    snprintf(a.days, sizeof(a.days), "%s", v->days);
    a.on = true;
    snprintf(a.label, sizeof(a.label), "%s", v->label[0] ? v->label : "Alarm");
    if (editing >= 0 && editing < g_alarms.count)
    {
        g_alarms.a[editing] = a;
        ui_toast("Alarm set: %s", clock);
    }
    else if (alarm_add(&g_alarms, &a) < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Alarm list full.");
        return;
    }
    else
    {
        collect();
        list_show(&list, timer_n + g_alarms.count - 1);
        ui_toast("Alarm set: %s", clock);
    }
    sfx_play(SFX_SAVED);
    save_alarms();
}

static void new_alarm(void)
{
    open_new(PICK_ALARM);
}

static void edit_sel(void)
{
    collect();
    if (!on_alarm())
        return;
    editing = list.sel - timer_n;
    const Alarm *a = &g_alarms.a[editing];
    PickValue v;
    v.a = a->minute / 60;
    v.b = a->minute % 60;
    snprintf(v.days, sizeof(v.days), "%s", a->days);
    snprintf(v.label, sizeof(v.label), "%s", a->label);
    picker_open(PICK_ALARM, "Edit alarm", &v, alarm_picked);
}

// Pauses or resumes a countdown; switches an alarm on or off.
static void toggle(int i)
{
    collect();
    if (i < 0 || i >= list.count)
        return;
    if (i < timer_n)
    {
        timers_toggle(timer_slots[i], ui_now());
        sfx_play(g_timers[timer_slots[i]].running ? SFX_TIMER : SFX_UNCHECK);
        ui_dirty();
    }
    else
    {
        Alarm *a = &g_alarms.a[i - timer_n];
        a->on = !a->on;
        sfx_play(a->on ? SFX_STAMP : SFX_UNCHECK);
        save_alarms();
    }
}

// Undo after a delete: the alarms, or the countdowns, as they were.
static AlarmList undo_alarms;
static Countdown undo_timers[TIMER_MAX];

static void alarm_undo(void)
{
    g_alarms = undo_alarms;
    save_alarms();
}

static void timer_undo(void)
{
    memcpy(g_timers, undo_timers, sizeof(g_timers));
    collect();
    ui_dirty();
}

static void delete_alarm_yes(void)
{
    collect();
    if (!on_alarm())
        return;
    undo_alarms = g_alarms;
    alarm_remove(&g_alarms, list.sel - timer_n);
    sfx_play(SFX_DELETE);
    save_alarms();
    ui_undo_offer(alarm_undo);
}

static void delete_sel(void)
{
    collect();
    if (on_timer())
    {
        // A countdown goes in one tap, without asking: Undo brings it back.
        memcpy(undo_timers, g_timers, sizeof(g_timers));
        timers_remove(timer_slots[list.sel]);
        sfx_play(SFX_DELETE);
        collect();
        ui_dirty();
        ui_undo_offer(timer_undo);
    }
    else if (on_alarm())
    {
        const Alarm *a = &g_alarms.a[list.sel - timer_n];
        ui_confirm("Delete alarm?", a->label, "Delete", delete_alarm_yes);
    }
}

static int menu_map[4];

static void more_done(int i)
{
    switch (menu_map[i])
    {
        case 0: toggle(list.sel); break;
        case 1: edit_sel(); break;
        case 2: delete_sel(); break;
        case 3: start_seconds(last_seconds); break;
    }
}

static void do_more(void)
{
    collect();
    SheetItem items[4];
    int n = 0;
    static char again[28];
    if (on_timer())
    {
        items[n] = (SheetItem){ "Pause or resume", ICON_PAUSE, SEC_PLAIN };
        menu_map[n++] = 0;
    }
    else if (on_alarm())
    {
        items[n] = (SheetItem){ "On or off", ICON_BELL, SEC_PLAIN };
        menu_map[n++] = 0;
        items[n] = (SheetItem){ "Edit alarm", ICON_EDIT, SEC_PLAIN };
        menu_map[n++] = 1;
    }
    if (list.count > 0)
    {
        items[n] = (SheetItem){ "Delete", ICON_TRASH, SEC_ALERT };
        menu_map[n++] = 2;
    }
    if (last_seconds % 60)
        snprintf(again, sizeof(again), "Again: %d:%02d", last_seconds / 60,
                 last_seconds % 60);
    else
        snprintf(again, sizeof(again), "Again: %d min", last_seconds / 60);
    items[n] = (SheetItem){ again, ICON_TIMERS, SEC_PLAIN };
    menu_map[n++] = 3;
    ui_sheet("Timers", items, n, more_done);
}

// Seconds only change on screen while a countdown runs; otherwise the
// minute tick (header clock) is enough.
static bool ticking(void)
{
    for (int i = 0; i < TIMER_MAX; i++)
        if (g_timers[i].used && g_timers[i].running)
            return true;
    return false;
}

static void update(const Input *in)
{
    collect();
    // While a countdown runs, its row counts down too.
    static time_t shown;
    if (ui_now() != shown)
    {
        shown = ui_now();
        if (ticking())
            ui_dirty_bottom();
    }

    Rect r[STRIP_KEYS];
    strip_rects(r);
    int p = press_update(&strip, in, r, STRIP_KEYS);
    if (p == 0)
    {
        new_timer();
        return;
    }
    if (p > 0)
    {
        start_seconds(p <= QUICK_N ? QUICK_MIN[p - 1] * 60 : last_seconds);
        return;
    }
    int b = bar_update(&bar, in);
    if (b == 0)
    {
        if (on_timer())
            toggle(list.sel);
        else
            edit_sel();
    }
    else if (b == 1)
    {
        new_alarm();
    }
    if (b >= 0)
        return;

    ListEvent ev = list_update(&list, in);
    if (ev.kind == LE_BOX)
        toggle(ev.index);
    else if (ev.kind == LE_OPEN && on_alarm())
        edit_sel();
    else if (ev.kind == LE_OPEN)
        toggle(ev.index);
    if (in->down & KEY_START)
        start_seconds(last_seconds);
}

static void enter(void)
{
    if (list.height == 0)
        list_init(&list, LIST_TOP, UI_BAR_Y - LIST_TOP, BOX_W);
    missed_clear();
    collect();
    // Nothing set yet: straight on to a new timer. Only when the page was
    // just opened; Back from the new timer page stays here.
    if (ui_pushed() && list.count == 0)
        new_timer();
}

// The header's trash: a countdown or an alarm is picked.
static bool can_delete(void)
{
    collect();
    return on_timer() || on_alarm();
}

const Screen SCR_TIMERS = {
    .enter = enter,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .ticking = ticking,
    .title = "Timers",
    .icon = ICON_TIMERS,
    .sec = SEC_TIMERS,
    .on_add = new_timer,   // X: a new timer (its page switches to an alarm)
    .on_more = do_more,
    .on_delete = delete_sel,
    .can_delete = can_delete,
};
