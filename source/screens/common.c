#include "screens.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <nds.h>

#include "device.h"
#include "launch/launch.h"
#include "sfx.h"
#include "ui/editor.h"
#include "store/holidays.h"
#include "store/work.h"

// Rows of a list ask for today again and again: the date is worked out once
// a second, not on every call.
Date today_date(void)
{
    static time_t at = (time_t)-1;
    static Date today;
    time_t now = ui_now();
    if (now != at)
    {
        struct tm t;
        localtime_r(&now, &t);
        today = (Date){ t.tm_year + 1900, t.tm_mon + 1, t.tm_mday };
        at = now;
    }
    return today;
}

void todo_expand_typed(const char *typed, char *out, size_t size)
{
    snprintf(out, size, "%s", typed);
    if (todo_expand_due(out, size, today_date()) < 0)
        ui_toast("Date not understood.");
}

void draw_clock_header(Surface *s, const char *title)
{
    time_t now = ui_now();
    struct tm t;
    localtime_r(&now, &t);
    char right[24], clock[12];
    time_fmt12(t.tm_hour * 60 + t.tm_min, TIME_MIN, clock, sizeof(clock));
    snprintf(right, sizeof(right), "%s %d %s %s", DATE_DAY3[t.tm_wday % 7], t.tm_mday,
             DATE_MONTH3[t.tm_mon % 12], clock);
    gfx_header(s, title, right);
}

int text_wrap(const char *text, int width, char lines[][TEXT_WRAP_W + 1], int max_lines)
{
    if (width > TEXT_WRAP_W)
        width = TEXT_WRAP_W;
    int n = 0;
    while (*text && n < max_lines)
    {
        while (*text == ' ')
            text++;
        int len = (int)strlen(text);
        if (len == 0)
            break;
        int cut = len;
        if (len > width)
        {
            // Break at the last space that fits; a longer word is split.
            cut = width;
            for (int i = width; i > 0; i--)
                if (text[i] == ' ')
                {
                    cut = i;
                    break;
                }
        }
        memcpy(lines[n], text, (size_t)cut);
        lines[n][cut] = '\0';
        text += cut;
        n++;
    }
    while (*text == ' ')
        text++;
    if (*text && n > 0)
    {
        // More text than lines: mark the cut.
        int last = (int)strlen(lines[n - 1]);
        if (last >= width)
            last = width - 1;
        lines[n - 1][last] = '~';
        lines[n - 1][last + 1] = '\0';
    }
    return n;
}

uint8_t event_ink(const CalEvent *e)
{
    if (cal_is_person(e))
        return C_ALERT;
    if (cal_is_work(e))
        return C_BROWN;
    return C_INK;
}

static char pending_path[128];
static int pending_frames = 0;

void request_launch(const char *full_path)
{
    snprintf(pending_path, sizeof(pending_path), "%s", full_path);
    pending_frames = 34;  // Time for the jingle and the note to show.
    sfx_play(SFX_LAUNCH);
    ui_toast("Starting %s", full_path);
}

void launch_poll(void)
{
    if (pending_frames == 0 || --pending_frames > 0)
        return;
    device_flush();
    handwriting_save();
    LaunchResult r = launch_nds(pending_path);  // Returns only on failure.
    device_cpu_fast(false);                     // still here: back to half speed
    ui_toast("%s", launch_error_text(r));
}

// ---------------------------------------------------------------- details

int title_draw(Surface *s, const char *text, int row, uint8_t ink)
{
    if (strlen(text) <= 14)
    {
        // Short enough to write big, over two rows.
        gfx_text2x(s, 20, PAPER_TEXT2X(row + 1), text, ink);
        return row + 2;
    }
    char lines[5][TEXT_WRAP_W + 1];
    int n = text_wrap(text, 29, lines, 5);
    for (int i = 0; i < n; i++, row++)
        gfx_text(s, 20, PAPER_TEXT(row), lines[i], ink);
    return row;
}

int todo_detail_draw(Surface *s, const TodoItem *it, Date today, const char *right)
{
    char buf[64];
    long days = it->has_due ? date_to_days(it->due) - date_to_days(today) : 0;
    bool late = it->has_due && !it->done && days < 0;

    if (it->done)
        gfx_chip(s, 20, PAPER_Y, "DONE", SEC_MID(SEC_TODO), C_INK);
    else if (late)
        gfx_chip(s, 20, PAPER_Y, "LATE", SEC_MID(SEC_ALERT), C_SNOW);
    else
        gfx_chip(s, 20, PAPER_Y, "TO DO", C_SEL, C_INK);
    if (right != NULL)
        gfx_text(s, SCREEN_W - 8 - (int)strlen(right) * FONT_W, PAPER_TEXT(0), right, C_INK_SOFT);
    int row = title_draw(s, it->text, 1, it->done ? C_INK_SOFT : C_INK) + 1;

    int x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "DUE", SEC_MID(SEC_CAL), C_SNOW);
    if (it->has_due)
    {
        char rel[24] = "";
        if (days == 0)
            snprintf(rel, sizeof(rel), ", today");
        else if (days == 1)
            snprintf(rel, sizeof(rel), ", tomorrow");
        else if (days > 1 && days < 100)
            snprintf(rel, sizeof(rel), ", in %ld days", days);
        else if (days == -1 && !it->done)
            snprintf(rel, sizeof(rel), ", 1 day late");
        else if (days < 0 && days > -1000 && !it->done)
            snprintf(rel, sizeof(rel), ", %ldd late", -days);
        snprintf(buf, sizeof(buf), "%s %d %s%s", DATE_DAY3[date_weekday(it->due)], it->due.d,
                 DATE_MONTH3[it->due.m - 1], rel);
        gfx_text_n(s, x + 5, PAPER_TEXT(row), buf, 24, late ? C_ALERT : C_INK);
    }
    else
    {
        gfx_text(s, x + 5, PAPER_TEXT(row), "no date", C_INK_SOFT);
    }
    row++;
    x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "REPEAT", SEC_MID(SEC_TIMERS), C_SNOW);
    if (it->repeat)
    {
        todo_repeat_text(it->repeat, it->every, buf, sizeof(buf));
        gfx_text_n(s, x + 5, PAPER_TEXT(row), buf, 21, C_INK);
    }
    else
    {
        gfx_text(s, x + 5, PAPER_TEXT(row), "never", C_INK_SOFT);
    }
    return row + 1;
}

void date_block_draw(Surface *s, int x, int y, Date d)
{
    static const char *MONU[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    int w = 42, h = 42;
    char num[4];
    gfx_round_fill(s, x + 2, y + 2, w, h, 3, C_SHADOW);
    gfx_panel(s, x, y, w, h, 3, C_WHITE, C_INK);
    gfx_round_fill(s, x + 1, y + 1, w - 2, 8, 2, SEC_MID(SEC_CAL));
    gfx_fill(s, x + 1, y + 5, w - 2, 7, SEC_MID(SEC_CAL));
    gfx_hline(s, x + 1, y + 12, w - 2, C_INK);
    gfx_text_center(s, x + w / 2, y + 3, MONU[(d.m + 11) % 12], C_SNOW);
    snprintf(num, sizeof(num), "%d", d.d % 100);
    gfx_text2x(s, x + w / 2 - (int)strlen(num) * 8, y + 15, num, C_INK);
    gfx_text_center(s, x + w / 2, y + 32, DATE_DAY3[date_weekday(d)], C_INK_SOFT);
}

int day_holidays(Date day, const char **out, int max)
{
    return device_holidays() ? holidays_on(day, out, max) : 0;
}

// ---------------------------------------------------------------- clock face

// sin of a/60 of a turn, times 256 (a = 0 points up for the hands below).
static int sin60(int a)
{
    static const short Q[16] = { 0, 27, 53, 79, 104, 128, 150, 171,
                                 190, 207, 222, 234, 243, 250, 255, 256 };
    a %= 60;
    if (a < 0)
        a += 60;
    if (a <= 15)
        return Q[a];
    if (a <= 30)
        return Q[30 - a];
    if (a <= 45)
        return -Q[a - 30];
    return -Q[60 - a];
}

static void hand(Surface *s, int cx, int cy, int a, int len, int thick, uint8_t c)
{
    gfx_line(s, cx, cy, cx + sin60(a) * len / 256, cy - sin60(a + 15) * len / 256, c, thick);
}

void clock_draw(Surface *s, int cx, int cy, int r, int minute, bool bells)
{
    if (bells)
    {
        // An alarm clock: two bells on top, two feet below.
        int bx = r * 7 / 10, by = r * 8 / 10, br = r / 3;
        for (int side = -1; side <= 1; side += 2)
        {
            gfx_line(s, cx + side * r / 2, cy + r - 2, cx + side * (r * 8 / 10), cy + r + 4,
                     C_INK, 3);
            gfx_disc(s, cx + side * bx, cy - by, br + 1, C_INK);
            gfx_disc(s, cx + side * bx, cy - by, br - 1, SEC_MID(SEC_TIMERS));
        }
        gfx_fill(s, cx - 2, cy - r - 5, 5, 5, C_INK);   // the hammer between them
    }
    gfx_disc(s, cx, cy, r + 2, C_INK);
    gfx_disc(s, cx, cy, r, C_WHITE);
    for (int i = 0; i < 12; i++)
    {
        int px = cx + sin60(i * 5) * (r - 4) / 256, py = cy - sin60(i * 5 + 15) * (r - 4) / 256;
        if (i % 3 == 0)
            gfx_fill(s, px - 1, py - 1, 3, 3, C_INK);
        else
            gfx_fill(s, px, py, 2, 2, C_INK_SOFT);
    }
    minute = ((minute % 1440) + 1440) % 1440;
    hand(s, cx, cy, (minute / 60 % 12) * 5 + (minute % 60) / 12, r * 5 / 10, 3, C_INK);
    hand(s, cx, cy, minute % 60, r * 8 / 10, 2, C_ACCENT);
    gfx_disc(s, cx, cy, 2, C_ALERT);
}
