// Time picker for alarms and countdowns: the time on wheels.
//
// Bottom screen: a wheel for each part of the time (hour, minute, AM/PM for
// an alarm; minutes, seconds for a timer). Roll a wheel with the stylus, or
// tap the arrow above or below it for one step (hold it to keep going).
// Beside the wheels: day sets for an alarm, quick steps for a timer. Below:
// the alarm's days and label, or the minute keys that start a timer at
// once. Top screen: an alarm clock showing the time.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <nds.h>

#include "device.h"
#include "screens.h"
#include "sfx.h"
#include "store/date.h"
#include "ui/editor.h"
#include "ui/widgets.h"

#define UP_Y 27
#define ARROW_H 18
#define WIN_Y 46
#define WIN_H 48
#define DOWN_Y 95
#define SIDE_X 176
#define SIDE_W 76
#define ROW_Y 117
#define ROW_H2 23
#define LABEL_Y 142
#define LABEL_H 19
#define ROLL_PX 8                 // stylus travel for one step of a wheel
#define FIELD_AMPM 2

static const Screen picker_screen;

static const char DAY_LETTERS[8] = "MTWTFSS";
static const int PRESETS[7] = { 1, 3, 5, 10, 15, 25, 30 };
static const char *const DAY_SETS[3] = { "MTWTFSS", "MTWTF--", "-----SS" };

static PickMode mode;
static PickValue val;
static PickDone on_done;
// A new timer or alarm can switch between the two (the header key, or Y):
// each keeps its own value and done. Editing an alarm cannot.
static bool can_switch = false;
static PickValue vals[2];         // by PickMode
static PickDone dones[2];
static char title[24];
static int focus = 0;             // the wheel the D-pad turns
static int hold_field = -1, hold_dir = 0, hold_frames = 0;   // an arrow held down
static int roll_field = -1, roll_y = 0;                       // a wheel under the stylus
static bool roll_moved = false;
static Press side, row, label_key;
static Bar bar;

static int fields(void)
{
    return mode == PICK_ALARM ? 3 : 2;
}

// A wheel's column: x and width.
static void field_col(int f, int *x, int *w)
{
    if (mode == PICK_ALARM)
    {
        *x = 6 + f * 56;
        *w = 50;
    }
    else
    {
        *x = 6 + f * 84;
        *w = 78;
    }
}

static int field_max(int field)
{
    if (field == 1)
        return 59;
    return mode == PICK_ALARM ? 23 : 99;
}

// Moves a field by d steps of `step`. Alarm fields wrap around; the timer's
// minutes stop at 0 and 99. A step of 5 or 10 snaps to a multiple of it.
static void change(int field, int d, int step)
{
    if (field == FIELD_AMPM)
    {
        val.a = (val.a + 12) % 24;
        ui_dirty();
        return;
    }
    int *v = field == 0 ? &val.a : &val.b;
    int max = field_max(field);
    int n;
    if (step > 1)
        n = d > 0 ? (*v / step + 1) * step : ((*v + step - 1) / step - 1) * step;
    else
        n = *v + d;
    bool wrap = mode == PICK_ALARM || field == 1;
    if (n > max)
        n = wrap ? 0 : max;
    if (n < 0)
        n = wrap ? (max / step) * step : 0;
    *v = n;
    ui_dirty();
}

// Fast steps once an arrow has been held for a second (minutes and seconds).
static int hold_step(int field)
{
    bool hours = mode == PICK_ALARM && field == 0;
    return (!hours && field != FIELD_AMPM && hold_frames > 60) ? 5 : 1;
}

static void cycle_days(void)
{
    int next = 0;
    for (int i = 0; i < 2; i++)
        if (strcmp(val.days, DAY_SETS[i]) == 0)
            next = i + 1;
    snprintf(val.days, sizeof(val.days), "%s", DAY_SETS[next]);
    ui_dirty();
}

static void finish(void)
{
    if (mode == PICK_TIMER && val.a == 0 && val.b == 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Set a time first.");
        return;
    }
    PickValue v = val;
    PickDone cb = can_switch ? dones[mode] : on_done;
    ui_pop();
    if (cb != NULL)
        cb(&v);
    ui_dirty();
}

static void label_done(bool ok, const char *text)
{
    if (ok)
        snprintf(val.label, sizeof(val.label), "%s", text);
    ui_dirty();
}

static void edit_label(void)
{
    editor_open("ALARM LABEL", "Shown when it rings.", val.label, 40, label_done);
}

// ---------------------------------------------------------------- input

static void side_rects(Rect *r)
{
    for (int i = 0; i < 3; i++)
        r[i] = (Rect){ SIDE_X, (short)(UP_Y + i * 30), SIDE_W, 26 };
}

static void row_rects(Rect *r)
{
    for (int i = 0; i < 7; i++)
        r[i] = (Rect){ (short)(4 + i * 36), ROW_Y, 33, ROW_H2 };
}

// The wheel part under the stylus: field, and -1 its up arrow, 0 the wheel,
// +1 its down arrow. False when there is none.
static bool wheel_at(int tx, int ty, int *field, int *part)
{
    for (int f = 0; f < fields(); f++)
    {
        int x, w;
        field_col(f, &x, &w);
        if (tx < x || tx >= x + w)
            continue;
        *field = f;
        if (ty >= UP_Y && ty < UP_Y + ARROW_H)
            *part = -1;
        else if (ty >= WIN_Y && ty < WIN_Y + WIN_H)
            *part = 0;
        else if (ty >= DOWN_Y && ty < DOWN_Y + ARROW_H)
            *part = +1;
        else
            return false;
        return true;
    }
    return false;
}

static void wheels_touch(const Input *in)
{
    if (in->touch_down)
    {
        int f, part;
        hold_field = roll_field = -1;
        hold_frames = 0;
        if (!wheel_at(in->tx, in->ty, &f, &part))
            return;
        focus = f;
        if (part == 0)
        {
            roll_field = f;
            roll_y = in->ty;
            roll_moved = false;
        }
        else
        {
            // The up arrow brings the next higher number.
            hold_field = f;
            hold_dir = -part;
            change(f, hold_dir, 1);
            device_click();
        }
        ui_dirty();
        return;
    }
    if (in->touching && roll_field >= 0)
    {
        // The numbers follow the stylus: pulling down brings the higher one,
        // which sits above, into the middle.
        while (in->ty - roll_y >= ROLL_PX)
        {
            change(roll_field, +1, 1);
            roll_y += ROLL_PX;
            roll_moved = true;
            device_click();
        }
        while (in->ty - roll_y <= -ROLL_PX)
        {
            change(roll_field, -1, 1);
            roll_y -= ROLL_PX;
            roll_moved = true;
            device_click();
        }
        return;
    }
    if (in->touching && hold_field >= 0)
    {
        hold_frames++;
        int f, part;
        if ((in->repeat & KEY_TOUCH) && wheel_at(in->tx, in->ty, &f, &part) &&
            f == hold_field && part == -hold_dir)
        {
            change(hold_field, hold_dir, hold_step(hold_field));
            device_click();
        }
        return;
    }
    if (!in->touching)
    {
        // A tap on AM/PM flips it.
        if (in->touch_up && roll_field == FIELD_AMPM && !roll_moved)
        {
            change(FIELD_AMPM, 1, 1);
            device_click();
        }
        if (hold_field >= 0 || roll_field >= 0)
            ui_dirty_bottom();
        hold_field = roll_field = -1;
    }
}

static void update(const Input *in)
{
    Rect r[7];
    side_rects(r);
    int k = press_update(&side, in, r, 3);
    if (k >= 0 && mode == PICK_ALARM)
    {
        snprintf(val.days, sizeof(val.days), "%s", DAY_SETS[k]);
        ui_dirty();
    }
    else if (k == 0 || k == 1)
    {
        int add = k == 0 ? 1 : 5;
        val.a = val.a + add > 99 ? 99 : val.a + add;
        ui_dirty();
    }
    else if (k == 2)
    {
        val.a = val.b = 0;
        ui_dirty();
    }

    row_rects(r);
    k = press_update(&row, in, r, 7);
    if (k >= 0 && mode == PICK_ALARM)
    {
        val.days[k] = val.days[k] == '-' ? DAY_LETTERS[k] : '-';
        ui_dirty();
    }
    else if (k >= 0)
    {
        val.a = PRESETS[k];   // a minute key starts the timer at once
        val.b = 0;
        finish();
        return;
    }

    if (mode == PICK_ALARM)
    {
        Rect lr = { 4, LABEL_Y, SCREEN_W - 8, LABEL_H };
        if (press_update(&label_key, in, &lr, 1) == 0)
        {
            edit_label();
            return;
        }
    }
    if (bar_update(&bar, in) == 0)
    {
        finish();
        return;
    }
    wheels_touch(in);

    // Buttons.
    if ((in->down & KEY_LEFT) && focus > 0)
        focus--, ui_dirty();
    if ((in->down & KEY_RIGHT) && focus < fields() - 1)
        focus++, ui_dirty();
    if (in->held & (KEY_UP | KEY_DOWN))
        hold_frames++;
    else if (!in->touching)
        hold_frames = 0;
    if (in->repeat & KEY_UP)
        change(focus, +1, hold_step(focus));
    if (in->repeat & KEY_DOWN)
        change(focus, -1, hold_step(focus));
    if (in->repeat & KEY_R)
        change(focus, +1, 10);
    if (in->repeat & KEY_L)
        change(focus, -1, 10);
    if (mode == PICK_ALARM && (in->down & KEY_X))
        edit_label();
    else if (mode == PICK_ALARM && !can_switch && (in->down & KEY_Y))
        cycle_days();
    else if (in->down & (KEY_A | KEY_START))
        finish();
}

// ---------------------------------------------------------------- top

// "Rings in 7 h 05 min" / "Rings Tue 07:30" / "No days: never rings".
static void alarm_summary(char *out, int size, uint8_t *color)
{
    time_t now = ui_now();
    struct tm t;
    localtime_r(&now, &t);
    Date today = { t.tm_year + 1900, t.tm_mon + 1, t.tm_mday };
    int now_min = t.tm_hour * 60 + t.tm_min;
    int at = val.a * 60 + val.b;
    *color = C_INK;
    for (int dd = 0; dd <= 7; dd++)
    {
        Date day = date_add_days(today, dd);
        int wd = date_weekday(day);
        if (val.days[(wd + 6) % 7] == '-')
            continue;
        int delta = dd * 1440 + at - now_min;
        if (delta <= 0)
            continue;
        if (delta < 60)
            snprintf(out, (size_t)size, "Rings in %d min", delta);
        else if (delta < 1440)
            snprintf(out, (size_t)size, "Rings in %d h %02d min", delta / 60, delta % 60);
        else
        {
            char clock[12];
            time_fmt12(at, TIME_MIN, clock, sizeof(clock));
            snprintf(out, (size_t)size, "Rings %s at %s", DATE_DAY3[wd], clock);
        }
        return;
    }
    snprintf(out, (size_t)size, "No days. Never rings.");
    *color = C_ALERT;
}

static void draw_top(Surface *s)
{
    char t[16], line[48];
    gfx_header(s, title, NULL);
    gfx_fill(s, 0, 18, SCREEN_W, SCREEN_H - 18, C_PAPER);
    gfx_rings(s, true);

    const int CX = 58, CY = 80, TX = 108;
    if (mode == PICK_ALARM)
    {
        // An alarm clock set to the time, and the time in big digits.
        clock_draw(s, CX, CY, 32, val.a * 60 + val.b, true);
        int h12 = val.a % 12 == 0 ? 12 : val.a % 12;
        snprintf(t, sizeof(t), "%d:%02d", h12, val.b);
        int end = gfx_text_scaled(s, TX, 46, t, C_INK, 3);
        gfx_text(s, end + 2, 62, val.a >= 12 ? "PM" : "AM", C_ACCENT);

        for (int i = 0; i < 7; i++)
        {
            int x = TX + i * 18;
            char c[2] = { DAY_LETTERS[i], 0 };
            bool on = val.days[i] != '-';
            gfx_round_fill(s, x, 78, 16, 13, 3, on ? SEC_MID(SEC_TIMERS) : C_PAPER_SH);
            gfx_text(s, x + 4, 81, c, on ? C_SNOW : C_DIM);
        }
        gfx_icon(s, TX - 2, 96, ICON_BELL, 1);
        gfx_text_n(s, TX + 18, 100, val.label[0] ? val.label : "Alarm", 15, C_INK);

        uint8_t color;
        alarm_summary(line, sizeof(line), &color);
        gfx_text_center(s, 128, 126, line, color);
        gfx_text_center(s, 128, 146, "Roll the wheels.", C_INK_SOFT);
        static const char *const KEYS[] = { "A", "Done", "X", "Label", "Y", "Days", NULL };
        static const char *const NEW_KEYS[] = { "A", "Done", "X", "Label", "Y", "Timer", NULL };
        legend_draw(s, PAPER_TEXT(13), can_switch ? NEW_KEYS : KEYS);
        return;
    }

    // A timer: the clock shows when it will ring.
    time_t end_time = ui_now() + val.a * 60 + val.b;
    struct tm e;
    localtime_r(&end_time, &e);
    clock_draw(s, CX, CY, 32, e.tm_hour * 60 + e.tm_min, true);
    snprintf(t, sizeof(t), "%02d:%02d", val.a, val.b);
    gfx_text_scaled(s, TX, 46, t, C_INK, 3);
    gfx_text_center(s, TX + 24, 74, "min", C_INK_SOFT);
    gfx_text_center(s, TX + 96, 74, "sec", C_INK_SOFT);
    if (val.a || val.b)
    {
        char clock[12];
        time_fmt12(e.tm_hour * 60 + e.tm_min, TIME_MIN, clock, sizeof(clock));
        snprintf(line, sizeof(line), "Rings at %s", clock);
        gfx_text(s, TX, 92, line, C_INK);
    }
    else
    {
        gfx_text(s, TX, 92, "No time set.", C_INK_SOFT);
    }
    gfx_text_center(s, 128, 126, "Roll the wheels, or tap", C_INK_SOFT);
    gfx_text_center(s, 128, 138, "a minute key to start.", C_INK_SOFT);
    static const char *const KEYS[] = { "A", "Start", NULL };
    static const char *const NEW_KEYS[] = { "A", "Start", "Y", "Alarm", NULL };
    legend_draw(s, PAPER_TEXT(13), can_switch ? NEW_KEYS : KEYS);
}

// ---------------------------------------------------------------- bottom

// What field f shows `d` steps from its value: "7", "30", "PM".
static void field_text(int f, int d, char *out, int size)
{
    if (f == FIELD_AMPM)
    {
        bool pm = val.a >= 12;
        snprintf(out, (size_t)size, "%s", (d == 0) == pm ? "PM" : "AM");
        return;
    }
    int max = field_max(f), v = (f == 0 ? val.a : val.b) + d;
    bool wrap = mode == PICK_ALARM || f == 1;
    if (v < 0 || v > max)
    {
        if (!wrap)
        {
            out[0] = '\0';   // the timer's minutes stop at the ends
            return;
        }
        v = (v + max + 1) % (max + 1);
    }
    if (mode == PICK_ALARM && f == 0)
        snprintf(out, (size_t)size, "%d", v % 12 == 0 ? 12 : v % 12);
    else
        snprintf(out, (size_t)size, "%02d", v);
}

static void draw_arrow(Surface *s, int cx, int cy, int dir, uint8_t c)
{
    // A small solid triangle, pointing up for dir < 0.
    for (int i = 0; i < 5; i++)
        gfx_fill(s, cx - i * 2, cy + dir * (2 - i) + (dir < 0 ? 0 : -1), i * 4 + 2, 1, c);
}

static void draw_wheel(Surface *s, int f)
{
    int x, w;
    field_col(f, &x, &w);
    int cx = x + w / 2;
    bool up = hold_field == f && hold_dir > 0, down = hold_field == f && hold_dir < 0;
    gfx_button(s, x, UP_Y, w, ARROW_H, NULL, ICON_NONE, SEC_PLAIN, up, true);
    draw_arrow(s, cx, UP_Y + ARROW_H / 2 - 1 + (up ? 1 : 0), -1, C_INK);
    gfx_button(s, x, DOWN_Y, w, ARROW_H, NULL, ICON_NONE, SEC_PLAIN, down, true);
    draw_arrow(s, cx, DOWN_Y + ARROW_H / 2 - 1 + (down ? 1 : 0), +1, C_INK);

    // The wheel: the value in the middle, its neighbors fading above and
    // below.
    bool on = focus == f && ui_pad_mode();
    gfx_panel(s, x, WIN_Y, w, WIN_H, 3, C_PAPER_SH, on ? C_ACCENT : C_INK_SOFT);
    gfx_fill(s, x + 1, WIN_Y + 13, w - 2, 22, C_WHITE);
    gfx_hline(s, x + 1, WIN_Y + 12, w - 2, C_RULE);
    gfx_hline(s, x + 1, WIN_Y + 35, w - 2, C_RULE);
    if (on)
        gfx_frame(s, x + 1, WIN_Y + 1, w - 2, WIN_H - 2, C_ACCENT);
    char t[8];
    field_text(f, +1, t, sizeof(t));    // the next higher sits above
    gfx_text_center(s, cx, WIN_Y + 3, t, C_DIM);
    field_text(f, 0, t, sizeof(t));
    gfx_text2x(s, cx - (int)strlen(t) * 8, WIN_Y + 16, t, C_INK);
    field_text(f, -1, t, sizeof(t));
    gfx_text_center(s, cx, WIN_Y + 38, t, C_DIM);
}

static void draw_bottom(Surface *s)
{
    gfx_page(s, UI_HEAD_H);
    for (int f = 0; f < fields(); f++)
        draw_wheel(s, f);
    // The colon between hours and minutes (or minutes and seconds).
    int x0, w0, x1, w1;
    field_col(0, &x0, &w0);
    field_col(1, &x1, &w1);
    int mid = (x0 + w0 + x1) / 2;
    gfx_fill(s, mid - 1, WIN_Y + 18, 2, 3, C_INK);
    gfx_fill(s, mid - 1, WIN_Y + 27, 2, 3, C_INK);

    // Beside the wheels.
    static const char *const SETS[3] = { "All week", "Weekdays", "Weekend" };
    static const char *const STEPS[3] = { "+1 min", "+5 min", "Clear" };
    Rect r[7];
    side_rects(r);
    for (int i = 0; i < 3; i++)
    {
        bool on = mode == PICK_ALARM && strcmp(val.days, DAY_SETS[i]) == 0;
        gfx_button(s, r[i].x, r[i].y, r[i].w, r[i].h, mode == PICK_ALARM ? SETS[i] : STEPS[i],
                   ICON_NONE, on ? SEC_TIMERS : SEC_PLAIN, on || press_is(&side, i), true);
    }

    // The days of an alarm, or the minute keys of a timer.
    row_rects(r);
    for (int i = 0; i < 7; i++)
    {
        char label[8];
        bool on = mode == PICK_ALARM && val.days[i] != '-';
        if (mode == PICK_ALARM)
            snprintf(label, sizeof(label), "%c", DAY_LETTERS[i]);
        else
            snprintf(label, sizeof(label), "%dm", PRESETS[i]);
        gfx_button(s, r[i].x, r[i].y, r[i].w, r[i].h, label, ICON_NONE,
                   on || mode == PICK_TIMER ? SEC_TIMERS : SEC_PLAIN, on || press_is(&row, i),
                   true);
    }

    if (mode == PICK_ALARM)
    {
        char label[64];
        snprintf(label, sizeof(label), "Label: %.20s", val.label[0] ? val.label : "Alarm");
        gfx_button(s, 4, LABEL_Y, SCREEN_W - 8, LABEL_H, label, ICON_NONE, SEC_NOTES,
                   press_is(&label_key, 0), true);
    }
    else
    {
        gfx_text_center(s, SCREEN_W / 2, LABEL_Y + 6, "Tap to start now.", C_INK_SOFT);
    }

    bar.n = 1;
    bar.narrow = 0;
    if (mode == PICK_ALARM)
        bar.item[0] = (BarItem){ "Done", ICON_CHECK, SEC_TODO, true };
    else
        bar.item[0] = (BarItem){ "Start", ICON_PLAY, SEC_TODO, true };
    bar_draw(s, &bar);
}

static const char *picker_title(void)
{
    return title;
}

static const Screen picker_screen = {
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title_fn = picker_title,
    .icon = ICON_TIMERS,
    .sec = SEC_TIMERS,
    // "Rings at" and "Rings in" change once a minute, which the minute tick
    // already redraws.
};

static void reset_input(void)
{
    focus = 0;
    hold_field = roll_field = -1;
    hold_frames = 0;
}

void picker_open(PickMode m, const char *t, const PickValue *init, PickDone done)
{
    mode = m;
    can_switch = false;
    snprintf(title, sizeof(title), "%s", t);
    val = *init;
    if (mode == PICK_ALARM && strlen(val.days) != 7)
        snprintf(val.days, sizeof(val.days), "MTWTFSS");
    reset_input();
    on_done = done;
    ui_push(&picker_screen);
}

// ---------------------------------------------------------------- new

static Screen new_screen;

// The header key switches to the other kind; its icon is that kind's.
static void set_mode(PickMode m)
{
    mode = m;
    val = vals[m];
    snprintf(title, sizeof(title), "%s", m == PICK_TIMER ? "New timer" : "New alarm");
    new_screen.more_icon = m == PICK_TIMER ? ICON_BELL : ICON_TIMERS;
    reset_input();
    ui_dirty();
}

static void switch_mode(void)
{
    vals[mode] = val;   // what was rolled stays for a switch back
    set_mode(mode == PICK_TIMER ? PICK_ALARM : PICK_TIMER);
}

void picker_open_new(PickMode start, const PickValue *timer_init, const PickValue *alarm_init,
                     PickDone timer_done, PickDone alarm_done)
{
    new_screen = picker_screen;
    new_screen.on_more = switch_mode;
    vals[PICK_TIMER] = *timer_init;
    vals[PICK_ALARM] = *alarm_init;
    if (strlen(vals[PICK_ALARM].days) != 7)
        snprintf(vals[PICK_ALARM].days, sizeof(vals[PICK_ALARM].days), "MTWTFSS");
    dones[PICK_TIMER] = timer_done;
    dones[PICK_ALARM] = alarm_done;
    can_switch = true;
    set_mode(start);
    ui_push(&new_screen);
}
