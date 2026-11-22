// Event time: the start and the end on wheels, like the alarm screen.
//
// Bottom screen: three tabs choose the kind: All day (no clock), Start only
// (no end) or Start, end. Under them, a set of wheels for the start and one
// for the end: hour, minute, AM/PM. Roll a wheel with the stylus, or tap the
// arrow above or below it (hold it to keep going). Minutes move in steps of
// five. Moving the start carries the end along. The keys under the wheels
// make the event run over more days. Top screen: a clock and the times in
// big letters.
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "device.h"
#include "screens.h"
#include "sfx.h"
#include "store/cal_io.h"
#include "ui/widgets.h"

#define TAB_Y 27
#define TAB_H 20
#define LABEL_Y 51                   // "Starts" and "Ends" over the wheels
#define UP_Y 61
#define ARROW_H 16
#define WIN_Y 78
#define WIN_H 44
#define DOWN_Y 123
#define DAYS_Y 142
#define DAYS_H 20
#define WHEEL_W 38
#define ROLL_PX 8                    // stylus travel for one step of a wheel
#define STEP 5                       // minutes per step
#define DAY_MIN 1440
#define FIELDS 6                     // start hour, minute, AM/PM; then the end's

enum { KIND_ALLDAY, KIND_AT, KIND_RANGE };
enum { PART_HOUR, PART_MIN, PART_AMPM };

static EvTime val;
static Date start_day;
static EvTimeDone on_done;
static char text[CAL_TEXT_MAX + 1];
static int focus = 0;                // the wheel the D-pad turns
static int hold_field = -1, hold_dir = 0;   // an arrow held down
static int roll_field = -1, roll_y = 0;     // a wheel under the stylus
static bool roll_moved = false;
static Press tabs, day_keys;
static Bar bar;

static int kind(void)
{
    return val.all_day ? KIND_ALLDAY : val.end == CAL_NO_END ? KIND_AT : KIND_RANGE;
}

// Wheels that can be turned now: the start's three, and the end's with an
// end time.
static int fields(void)
{
    return kind() == KIND_ALLDAY ? 0 : kind() == KIND_AT ? 3 : FIELDS;
}

// On one day the end has to come after the start.
static bool backwards(void)
{
    return kind() == KIND_RANGE && val.days == 0 && val.end <= val.start;
}

static int wrap_day(int m)
{
    return ((m % DAY_MIN) + DAY_MIN) % DAY_MIN;
}

// ---------------------------------------------------------------- changes

// The minute of the hour one step up or down from m, around the hour.
static int step_minute(int m, int dir)
{
    int n = dir > 0 ? (m / STEP + 1) * STEP : ((m + STEP - 1) / STEP - 1) * STEP;
    return ((n % 60) + 60) % 60;
}

// A time with one of its parts moved a step.
static int stepped(int t, int part, int dir)
{
    if (part == PART_HOUR)
        return wrap_day(t + dir * 60);
    if (part == PART_AMPM)
        return wrap_day(t + 720);
    return t / 60 * 60 + step_minute(t % 60, dir);
}

static void change(int field, int dir)
{
    if (field >= fields())
        return;
    int part = field % 3;
    if (field < 3)
    {
        // The end keeps its distance from the start, like moving a block.
        int len = kind() == KIND_RANGE && val.days == 0 ? val.end - val.start : 0;
        val.start = stepped(val.start, part, dir);
        if (len > 0)
            val.end = val.start + len < DAY_MIN ? val.start + len : DAY_MIN - 1;
    }
    else
    {
        val.end = stepped(val.end, part, dir);
    }
    ui_dirty();
}

static void set_kind(int k)
{
    if (k == KIND_ALLDAY)
    {
        val.all_day = true;
    }
    else
    {
        val.all_day = false;
        if (k == KIND_AT)
            val.end = CAL_NO_END;
        else if (val.end == CAL_NO_END)
            val.end = val.start + 60 < DAY_MIN ? val.start + 60 : DAY_MIN - 1;
    }
    if (focus >= fields())
        focus = 0;
    ui_dirty();
}

static void set_days(int days)
{
    if (days < 0 || days > CAL_SPAN_MAX)
        return;
    val.days = days;
    ui_dirty();
}

static void finish(void)
{
    if (backwards())
    {
        sfx_play(SFX_ERROR);
        ui_toast("It ends before it starts.");
        return;
    }
    EvTime v = val;
    EvTimeDone cb = on_done;
    ui_pop();
    if (cb != NULL)
        cb(&v);
}

// ---------------------------------------------------------------- input

static void tab_rects(Rect *r)
{
    r[0] = (Rect){ 3, TAB_Y, 66, TAB_H };
    r[1] = (Rect){ 72, TAB_Y, 90, TAB_H };
    r[2] = (Rect){ 165, TAB_Y, 88, TAB_H };
}

static void day_rects(Rect *r)
{
    r[0] = (Rect){ 4, DAYS_Y, 30, DAYS_H };
    r[1] = (Rect){ 104, DAYS_Y, 30, DAYS_H };
}

static int field_x(int f)
{
    return 4 + (f / 3) * 128 + (f % 3) * 41;
}

// The wheel part under the stylus: field, and -1 its up arrow, 0 the wheel,
// +1 its down arrow. False when there is none.
static bool wheel_at(int tx, int ty, int *field, int *part)
{
    for (int f = 0; f < fields(); f++)
    {
        int x = field_x(f);
        if (tx < x || tx >= x + WHEEL_W)
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
            change(f, hold_dir);
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
            change(roll_field, +1);
            roll_y += ROLL_PX;
            roll_moved = true;
            device_click();
        }
        while (in->ty - roll_y <= -ROLL_PX)
        {
            change(roll_field, -1);
            roll_y -= ROLL_PX;
            roll_moved = true;
            device_click();
        }
        return;
    }
    if (in->touching && hold_field >= 0)
    {
        int f, part;
        if ((in->repeat & KEY_TOUCH) && wheel_at(in->tx, in->ty, &f, &part) &&
            f == hold_field && part == -hold_dir)
        {
            change(hold_field, hold_dir);
            device_click();
        }
        return;
    }
    if (!in->touching)
    {
        // A tap on AM/PM flips it.
        if (in->touch_up && roll_field >= 0 && roll_field % 3 == PART_AMPM && !roll_moved)
        {
            change(roll_field, 1);
            device_click();
        }
        if (hold_field >= 0 || roll_field >= 0)
            ui_dirty_bottom();
        hold_field = roll_field = -1;
    }
}

static void update(const Input *in)
{
    Rect r[3];
    tab_rects(r);
    int t = press_update(&tabs, in, r, 3);
    if (t >= 0)
        set_kind(t);
    day_rects(r);
    int d = press_update(&day_keys, in, r, 2);
    if (d == 0)
        set_days(val.days - 1);
    else if (d == 1)
        set_days(val.days + 1);
    if (bar_update(&bar, in) == 0)
    {
        finish();
        return;
    }
    wheels_touch(in);

    // Buttons: Left and Right pick a wheel, Up and Down turn it, L and R
    // change the tab.
    int n = fields();
    if ((in->down & KEY_LEFT) && focus > 0)
        focus--, ui_dirty();
    if ((in->down & KEY_RIGHT) && focus < n - 1)
        focus++, ui_dirty();
    if (in->repeat & KEY_UP)
        change(focus, +1);
    if (in->repeat & KEY_DOWN)
        change(focus, -1);
    if ((in->down & KEY_L) && kind() > 0)
        set_kind(kind() - 1);
    if ((in->down & KEY_R) && kind() < KIND_RANGE)
        set_kind(kind() + 1);
    if (in->down & (KEY_A | KEY_START))
        finish();
}

// ---------------------------------------------------------------- top

static void draw_top(Surface *s)
{
    char buf[64], tm[16];
    gfx_header(s, "Time", NULL);
    gfx_paper(s, PAPER_Y);
    if (strlen(text) <= 14)
        title_draw(s, text, 0, C_INK);
    else
        gfx_text_n(s, 20, PAPER_TEXT(1), text, 29, C_INK);

    // The times, beside a clock that shows the start.
    gfx_fill(s, 17, PAPER_Y + 3 * PAPER_ROW, SCREEN_W - 17, 6 * PAPER_ROW - 1, C_PAPER);
    int cx = 56, cy = 92;
    if (val.all_day)
    {
        gfx_icon(s, cx - 16, cy - 16, ICON_SUN, 2);
        gfx_text2x(s, 100, cy - 16, "All day", C_INK);
        gfx_text(s, 100, cy + 6, "No time needed.", C_INK_SOFT);
    }
    else
    {
        clock_draw(s, cx, cy, 28, val.start, false);
        bool range = kind() == KIND_RANGE;
        gfx_chip(s, 96, 62, range ? "FROM" : "AT", SEC_MID(SEC_CAL), C_SNOW);
        time_fmt12(val.start, TIME_MIN, tm, sizeof(tm));
        gfx_text2x(s, 96, 75, tm, C_INK);
        if (range)
        {
            gfx_chip(s, 96, 96, "TO", SEC_MID(SEC_TIMERS), C_SNOW);
            time_fmt12(val.end, TIME_MIN, tm, sizeof(tm));
            gfx_text2x(s, 96, 109, tm, backwards() ? C_ALERT : C_INK);
        }
        else
        {
            gfx_text(s, 96, 100, "No end time.", C_INK_SOFT);
        }
    }

    // The day or days, and how long it lasts.
    int row = 9;
    Date end_day = date_add_days(start_day, val.days);
    int x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, val.days ? "DAYS" : "DAY",
                     SEC_MID(SEC_NOTES), C_INK);
    if (val.days == 0)
        snprintf(buf, sizeof(buf), "%s %d %s", DATE_DAY3[date_weekday(start_day)], start_day.d,
                 DATE_MONTH3[start_day.m - 1]);
    else
        snprintf(buf, sizeof(buf), "%s %d %s - %s %d %s", DATE_DAY3[date_weekday(start_day)],
                 start_day.d, DATE_MONTH3[start_day.m - 1], DATE_DAY3[date_weekday(end_day)],
                 end_day.d, DATE_MONTH3[end_day.m - 1]);
    gfx_text_n(s, x + 5, PAPER_TEXT(row), buf, 23, C_INK);
    row++;
    if (backwards())
    {
        gfx_text(s, 20, PAPER_TEXT(row), "It ends before it starts.", C_ALERT);
    }
    else if (val.days > 0)
    {
        snprintf(buf, sizeof(buf), "%d days", val.days + 1);
        gfx_text(s, 20, PAPER_TEXT(row), buf, C_INK_SOFT);
    }
    else if (kind() == KIND_RANGE)
    {
        int len = val.end - val.start;
        if (len % 60 == 0)
            snprintf(buf, sizeof(buf), "%d hour%s", len / 60, len == 60 ? "" : "s");
        else if (len < 60)
            snprintf(buf, sizeof(buf), "%d minutes", len);
        else
            snprintf(buf, sizeof(buf), "%d h %02d min", len / 60, len % 60);
        gfx_text(s, 20, PAPER_TEXT(row), buf, C_INK_SOFT);
    }

    if (!val.all_day)
        gfx_text(s, 20, PAPER_TEXT(12), "Roll the wheels.", C_INK_SOFT);
    static const char *const KEYS_HELP[] = { "A", "Done", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS_HELP);
}

// ---------------------------------------------------------------- bottom

// What field f shows `d` steps from its value: "7", "30", "PM".
static void field_text(int f, int d, char *out, int size)
{
    int t = f < 3 ? val.start : val.end, part = f % 3;
    if (d != 0)
        t = stepped(t, part, d);
    if (part == PART_HOUR)
        snprintf(out, (size_t)size, "%d", t / 60 % 12 == 0 ? 12 : t / 60 % 12);
    else if (part == PART_MIN)
        snprintf(out, (size_t)size, "%02d", t % 60);
    else
        snprintf(out, (size_t)size, "%s", t >= 720 ? "PM" : "AM");
}

// A small solid triangle, pointing up for dir < 0.
static void draw_arrow(Surface *s, int cx, int cy, int dir, uint8_t c)
{
    for (int i = 0; i < 4; i++)
        gfx_fill(s, cx - i * 2, cy + dir * (1 - i) + (dir < 0 ? 0 : -1), i * 4 + 2, 1, c);
}

static void draw_wheel(Surface *s, int f)
{
    int x = field_x(f), cx = x + WHEEL_W / 2;
    bool up = hold_field == f && hold_dir > 0, down = hold_field == f && hold_dir < 0;
    gfx_button(s, x, UP_Y, WHEEL_W, ARROW_H, NULL, ICON_NONE, SEC_PLAIN, up, true);
    draw_arrow(s, cx, UP_Y + ARROW_H / 2 - 1 + (up ? 1 : 0), -1, C_INK);
    gfx_button(s, x, DOWN_Y, WHEEL_W, ARROW_H, NULL, ICON_NONE, SEC_PLAIN, down, true);
    draw_arrow(s, cx, DOWN_Y + ARROW_H / 2 - 1 + (down ? 1 : 0), +1, C_INK);

    // The wheel: the value in the middle, its neighbors fading above and
    // below.
    bool on = focus == f && ui_pad_mode();
    gfx_panel(s, x, WIN_Y, WHEEL_W, WIN_H, 3, C_PAPER_SH, on ? C_ACCENT : C_INK_SOFT);
    gfx_fill(s, x + 1, WIN_Y + 12, WHEEL_W - 2, 20, C_WHITE);
    gfx_hline(s, x + 1, WIN_Y + 11, WHEEL_W - 2, C_RULE);
    gfx_hline(s, x + 1, WIN_Y + 32, WHEEL_W - 2, C_RULE);
    if (on)
        gfx_frame(s, x + 1, WIN_Y + 1, WHEEL_W - 2, WIN_H - 2, C_ACCENT);
    char t[8];
    field_text(f, +1, t, sizeof(t));    // the next higher sits above
    gfx_text_center(s, cx, WIN_Y + 2, t, C_DIM);
    field_text(f, 0, t, sizeof(t));
    gfx_text2x(s, cx - (int)strlen(t) * 8, WIN_Y + 14, t, C_INK);
    field_text(f, -1, t, sizeof(t));
    gfx_text_center(s, cx, WIN_Y + 34, t, C_DIM);
}

static void draw_bottom(Surface *s)
{
    gfx_page(s, UI_HEAD_H);

    static const char *const TAB[3] = { "All day", "Start only", "Start, end" };
    Rect r[3];
    tab_rects(r);
    for (int i = 0; i < 3; i++)
    {
        bool on = kind() == i;
        gfx_button(s, r[i].x, r[i].y, r[i].w, r[i].h, TAB[i], ICON_NONE,
                   on ? SEC_CAL : SEC_PLAIN, on || press_is(&tabs, i), true);
    }

    if (val.all_day)
    {
        // No hours to choose: a sunny sticker says so.
        gfx_round_fill(s, 42, 58, 176, 78, 4, C_SHADOW);
        gfx_panel(s, 40, 55, 176, 78, 4, C_STICKY, C_STICKY_SH);
        gfx_fill(s, 116, 52, 24, 6, C_PAPER_SH);
        gfx_icon(s, 112, 62, ICON_SUN, 2);
        gfx_text_center(s, 128, 100, "All day", C_INK);
        gfx_text_center(s, 128, 114, "Tap a tab to set a time.", C_INK_SOFT);
    }
    else
    {
        gfx_chip(s, 4, LABEL_Y - 2, "STARTS", SEC_MID(SEC_CAL), C_SNOW);
        for (int f = 0; f < 3; f++)
            draw_wheel(s, f);
        if (kind() == KIND_RANGE)
        {
            gfx_chip(s, 132, LABEL_Y - 2, "ENDS", SEC_MID(SEC_TIMERS), C_SNOW);
            for (int f = 3; f < FIELDS; f++)
                draw_wheel(s, f);
        }
        else
        {
            // No end: an empty slot where its wheels would be.
            gfx_panel(s, 132, UP_Y, 120, DOWN_Y + ARROW_H - UP_Y, 3, C_PAPER_SH, C_RULE);
            gfx_text_center(s, 192, WIN_Y + 12, "No end", C_INK_SOFT);
            gfx_text_center(s, 192, WIN_Y + 24, "time.", C_INK_SOFT);
        }
    }

    // How many days it runs.
    char buf[24];
    day_rects(r);
    gfx_button(s, r[0].x, r[0].y, r[0].w, r[0].h, "-", ICON_NONE, SEC_PLAIN,
               press_is(&day_keys, 0), val.days > 0);
    gfx_button(s, r[1].x, r[1].y, r[1].w, r[1].h, "+", ICON_NONE, SEC_PLAIN,
               press_is(&day_keys, 1), val.days < CAL_SPAN_MAX);
    if (val.days)
        snprintf(buf, sizeof(buf), "%d days", val.days + 1);
    else
        snprintf(buf, sizeof(buf), "1 day");
    gfx_text_center(s, 69, DAYS_Y + 6, buf, C_INK);
    gfx_text(s, 142, DAYS_Y + 6, "Days it runs", C_INK_SOFT);

    bar.n = 1;
    bar.narrow = 0;
    bar.item[0] = (BarItem){ "Done", ICON_CHECK, SEC_TODO, true };
    bar_draw(s, &bar);
}

static const Screen evtime_screen = {
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title = "Event time",
    .icon = ICON_TIMERS,
    .sec = SEC_CAL,
};

void evtime_open(const char *t, Date start, const EvTime *init, EvTimeDone done)
{
    snprintf(text, sizeof(text), "%s", t);
    start_day = start;
    val = *init;
    if (val.start < 0 || val.start >= DAY_MIN)
        val.start = 9 * 60;
    if (val.end != CAL_NO_END && (val.end < 0 || val.end >= DAY_MIN))
        val.end = DAY_MIN - 1;
    focus = 0;
    hold_field = roll_field = -1;
    on_done = done;
    ui_push(&evtime_screen);
}
