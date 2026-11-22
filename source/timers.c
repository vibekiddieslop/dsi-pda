#include "timers.h"

#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "app.h"
#include "device.h"
#include "sfx.h"
#include "store/date.h"

Countdown g_timers[TIMER_MAX];

static char missed[MISSED_MAX][40];
static int missed_n = 0;

static long last_minute_key;      // days * 1440 + minute of the last check
static bool ringing = false;
static char ring_title[16];
static char ring_label[48];
static time_t ring_started;
static int ring_frame = 0;

#define RING_LATE_SECONDS 90      // Later than this counts as missed.
#define RING_MAX_SECONDS 120
#define SCAN_MAX_MINUTES (7 * 1440)

static long minute_key(time_t t, Date *date, int *minute)
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    Date d = { tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday };
    if (date)
        *date = d;
    if (minute)
        *minute = tmv.tm_hour * 60 + tmv.tm_min;
    return date_to_days(d) * 1440 + tmv.tm_hour * 60 + tmv.tm_min;
}

static void add_missed_str(const char *text)
{
    if (missed_n >= MISSED_MAX)
    {
        memmove(missed[0], missed[1], sizeof(missed[0]) * (MISSED_MAX - 1));
        missed_n--;
    }
    snprintf(missed[missed_n++], sizeof(missed[0]), "%s", text);
}

// "HH:MM label" entry.
static void add_missed(int minute, const char *label)
{
    char t[48], clock[12];
    time_fmt12(minute, TIME_SHORT, clock, sizeof(clock));
    snprintf(t, sizeof(t), "%s %.30s", clock, label);
    add_missed_str(t);
}

// What is ringing goes on the missed list: "ALARM: Wake up".
static void ring_missed(void)
{
    char t[48];
    snprintf(t, sizeof(t), "%.8s: %.30s", ring_title, ring_label);
    add_missed_str(t);
}

static void start_ring(const char *title, const char *label, time_t now)
{
    if (ringing)
        ring_missed();   // Already ringing: the older one counts as missed.
    ringing = true;
    snprintf(ring_title, sizeof(ring_title), "%s", title);
    snprintf(ring_label, sizeof(ring_label), "%s", label);
    ring_started = now;
    ring_frame = 0;
    sfx_alarm(true);
    device_wake();
    ui_dirty();   // a new title and label, even when one was already ringing
}

static void ring_stop(void)
{
    ringing = false;
    sfx_alarm(false);
    ui_dirty();
}

void timers_init(time_t now)
{
    memset(g_timers, 0, sizeof(g_timers));
    missed_n = 0;
    last_minute_key = minute_key(now, NULL, NULL);
}

int timers_left(const Countdown *c, time_t now)
{
    if (!c->running)
        return c->remaining;
    long left = (long)(c->end - now);
    return left > 0 ? (int)left : 0;
}

int timers_add(int seconds, time_t now)
{
    if (seconds <= 0)
        return -1;
    for (int i = 0; i < TIMER_MAX; i++)
    {
        if (!g_timers[i].used)
        {
            g_timers[i] = (Countdown){ true, true, now + seconds, seconds, seconds, "" };
            return i;
        }
    }
    return -1;
}

void timers_toggle(int i, time_t now)
{
    Countdown *c = &g_timers[i];
    if (!c->used)
        return;
    if (c->running)
    {
        c->remaining = timers_left(c, now);
        c->running = false;
    }
    else if (c->remaining > 0)
    {
        c->end = now + c->remaining;
        c->running = true;
    }
}

void timers_remove(int i)
{
    if (i >= 0 && i < TIMER_MAX)
        g_timers[i].used = false;
}

int missed_count(void)
{
    return missed_n;
}

const char *missed_text(int i)
{
    return (i >= 0 && i < missed_n) ? missed[i] : "";
}

void missed_clear(void)
{
    missed_n = 0;
}

static void check_alarms_at(long key, bool late, time_t now)
{
    Date day = date_from_days(key / 1440);
    int minute = (int)(key % 1440);
    int wd = date_weekday(day);
    for (int i = 0; i < g_alarms.count; i++)
    {
        const Alarm *a = &g_alarms.a[i];
        if (!a->on || a->minute != minute || !alarm_on_weekday(a, wd))
            continue;
        if (late)
            add_missed(a->minute, a->label);
        else
            start_ring("ALARM", a->label, now);
    }
}

void timers_tick(time_t now)
{
    // Everything below depends only on the time in whole seconds, so the
    // other 59 frames of each second have nothing to do.
    static time_t last_now = -1;
    if (now == last_now)
        return;
    last_now = now;

    // Countdowns.
    for (int i = 0; i < TIMER_MAX; i++)
    {
        Countdown *c = &g_timers[i];
        if (!c->used || !c->running || now < c->end)
            continue;
        char label[32];
        if (c->label[0])
            snprintf(label, sizeof(label), "%s", c->label);
        else if (c->total % 60)
            snprintf(label, sizeof(label), "%d:%02d timer", c->total / 60, c->total % 60);
        else
            snprintf(label, sizeof(label), "%d min timer", c->total / 60);
        if (now - c->end <= RING_LATE_SECONDS)
            start_ring("TIMER", label, now);
        else
            add_missed_str("Timer ended. Lid was shut.");
        c->used = false;
        ui_dirty();
    }

    // Alarms: visit every minute boundary since the last check. A gap means
    // the console slept; anything older than RING_LATE_SECONDS is missed.
    long key = minute_key(now, NULL, NULL);
    if (key != last_minute_key)
    {
        long from = last_minute_key + 1;
        if (key < last_minute_key || key - from > SCAN_MAX_MINUTES)
            from = key;  // Clock changed a lot: do not replay a week of alarms.
        long now_sec_in_min = (long)(now % 60);
        for (long k = from; k <= key; k++)
        {
            long late_secs = (key - k) * 60 + now_sec_in_min;
            check_alarms_at(k, late_secs > RING_LATE_SECONDS, now);
        }
        last_minute_key = key;
        ui_dirty();
    }

    if (ringing && now - ring_started > RING_MAX_SECONDS)
    {
        ring_missed();
        ring_stop();
    }
}

bool timers_next_alarm(time_t now, char *out, int size)
{
    Date today;
    int minute;
    minute_key(now, &today, &minute);
    for (int dd = 0; dd < 8; dd++)
    {
        Date day = date_add_days(today, dd);
        int wd = date_weekday(day);
        int best = -1;
        for (int i = 0; i < g_alarms.count; i++)
        {
            const Alarm *a = &g_alarms.a[i];
            if (!a->on || !alarm_on_weekday(a, wd))
                continue;
            if (dd == 0 && a->minute <= minute)
                continue;
            if (best < 0 || a->minute < g_alarms.a[best].minute)
                best = i;
        }
        if (best >= 0)
        {
            const Alarm *a = &g_alarms.a[best];
            char clock[12];
            time_fmt12(a->minute, TIME_SHORT, clock, sizeof(clock));
            snprintf(out, (size_t)size, "%s %s %.24s", clock,
                     dd == 0 ? "today" : (dd == 1 ? "tmrw" : DATE_DAY3[wd]), a->label);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- overlay

static bool ring_active(void)
{
    return ringing;
}

// The bottom screen: a big STOP, and Snooze below it.
#define STOP_X 28
#define STOP_Y 20
#define STOP_W 200
#define STOP_H 96
#define SNOOZE_Y 128
#define SNOOZE_H 40

// Rings again in SNOOZE_SECONDS, as a countdown with the same label.
static void ring_snooze(void)
{
    time_t now = ui_now();
    int i = timers_add(SNOOZE_SECONDS, now);
    ring_stop();
    if (i < 0)
    {
        ui_toast("Cannot snooze. Max 4 timers.");
        return;
    }
    snprintf(g_timers[i].label, sizeof(g_timers[i].label), "%.23s", ring_label);
    struct tm tmv;
    time_t at = now + SNOOZE_SECONDS;
    localtime_r(&at, &tmv);
    char t[12];
    time_fmt12(tmv.tm_hour * 60 + tmv.tm_min, TIME_MIN, t, sizeof(t));
    ui_toast("Snoozed. Rings at %s.", t);
}

static void ring_update(const Input *in)
{
    // The picture only changes when the flash flips (ring_draw), every 15
    // frames: redraw then, not 60 times a second.
    if (++ring_frame % 15 == 0)
        ui_dirty();
    // Ignore input for half a second so a held button does not stop it.
    if (ring_frame <= 30)
        return;
    bool snooze = (in->down & KEY_Y) != 0 ||
                  (in->touch_down && in->ty >= SNOOZE_Y && in->ty < SNOOZE_Y + SNOOZE_H &&
                   in->tx >= STOP_X && in->tx < STOP_X + STOP_W);
    if (snooze)
        ring_snooze();
    else if (in->down || in->touch_down)
        ring_stop();
}

static void ring_draw(Surface *top, Surface *bot)
{
    // The page flashes and the bell swings until something is pressed.
    bool flash = (ring_frame / 15) % 2 == 0;
    gfx_clear(top, flash ? SEC_MID(SEC_ALERT) : C_LEATHER);
    gfx_icon(top, 128 - 24 + (flash ? -4 : 4), 10, ICON_BELL, 3);
    gfx_text2x(top, 128 - (int)strlen(ring_title) * 8, 66, ring_title, C_SNOW);
    gfx_text_center(top, 128, 90, ring_label, C_SNOW);
    char t[16];
    struct tm tmv;
    time_t now = ui_now();
    localtime_r(&now, &tmv);
    time_fmt12(tmv.tm_hour * 60 + tmv.tm_min, TIME_LONG, t, sizeof(t));
    gfx_text_scaled(top, 128 - (int)strlen(t) * 12, 118, t, C_GOLD, 3);

    gfx_clear(bot, C_LEATHER);
    gfx_round_fill(bot, STOP_X + 2, STOP_Y + 5, STOP_W, STOP_H, 4, C_LEATHER_SH);
    gfx_button(bot, STOP_X, STOP_Y, STOP_W, STOP_H, NULL, ICON_NONE, SEC_ALERT, false, true);
    gfx_text_scaled(bot, 128 - 48, STOP_Y + 36, "STOP", C_INK, 3);
    gfx_button(bot, STOP_X, SNOOZE_Y, STOP_W, SNOOZE_H, "Snooze 5 min", ICON_BELL, SEC_TIMERS,
               false, true);
    gfx_text_center(bot, 128, 176, "Y to snooze. Any key to stop.", C_LABEL);
}

const Overlay g_ring_overlay = { ring_active, ring_update, ring_draw };
