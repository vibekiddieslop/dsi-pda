// Home. Top screen: greeting, a tear-off date block and a big clock, then
// today's events, the to-dos that need doing, and one ticker line that
// takes turns showing habits, the next alarm and what is coming up.
// Bottom screen: six tiles with live badges.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <nds.h>

#include "app.h"
#include "device.h"
#include "screens.h"
#include "sfx.h"
#include "store/holidays.h"
#include "store/work.h"
#include "timers.h"
#include "ui/widgets.h"

#define COMING_DAYS 30   // Ticker: next event and birthday this far ahead
#define TODO_AHEAD 3     // Ticker: next to-do due within 3 days
#define TICK_FRAMES 240  // 4 seconds per ticker line
#define SHOWN 3          // events and to-dos listed

// SELECT on Home opens what the ticker shows.
enum { J_HABITS, J_TIMERS, J_DAY, J_TODO };

typedef struct {
    const char *tag;
    int sec;
    char text[48];
    uint8_t ink;
    int jump;        // what SELECT opens: J_*
    Date day;        // J_DAY: the day
} Tick;

// What the top screen shows, worked out when Home appears and once a
// minute rather than on every redraw.
static struct {
    bool valid;
    long minute;
    char greeting[40];
    int greet_icon;
    int ev_n, ev_total;
    char ev_time[SHOWN][16], ev_text[SHOWN][CAL_TEXT_MAX + 32];
    uint8_t ev_ink[SHOWN];
    int td_n, td_total, td_open;
    bool td_urgent;                   // something is due today or late
    char td_text[SHOWN][TODO_TEXT_MAX + 1];
    bool td_late[SHOWN];
    int tick_n;
    Tick tick[6];
    int habits_done;
} m;

// "tmrw", a weekday within the week, else "12/24".
static void when_text(int in_days, Date day, char *out, int size)
{
    if (in_days == 1)
        snprintf(out, (size_t)size, "Tmrw");
    else if (in_days < 7)
        snprintf(out, (size_t)size, "%s", DATE_DAY3[date_weekday(day)]);
    else
        snprintf(out, (size_t)size, "%d/%d", day.m % 100, day.d % 100);
}

// An event's text on Home: a work shift is just "Work" beside its hours
// ("11a-5:30p Work"); the calendar keeps the shift's name.
static void home_label(const CalEvent *e, Date day, char *out, int size)
{
    if (cal_is_work(e))
        snprintf(out, (size_t)size, "Work");
    else
        cal_label(e, day, out, size);
}

static void add_tick(const char *tag, int sec, uint8_t ink, const char *text, int jump, Date day)
{
    if (m.tick_n >= (int)(sizeof(m.tick) / sizeof(m.tick[0])))
        return;
    Tick *t = &m.tick[m.tick_n++];
    t->tag = tag;
    t->sec = sec;
    t->ink = ink;
    snprintf(t->text, sizeof(t->text), "%.47s", text);
    t->jump = jump;
    t->day = day;
}

static void build(void)
{
    time_t now = ui_now();
    struct tm t;
    localtime_r(&now, &t);
    Date today = { t.tm_year + 1900, t.tm_mon + 1, t.tm_mday };
    m.valid = true;
    m.minute = (long)(now / 60);

    // Greeting by the time of day, with the owner's name when it fits.
    const char *hello = t.tm_hour < 5    ? "Good night"
                        : t.tm_hour < 12 ? "Good morning"
                        : t.tm_hour < 18 ? "Good afternoon"
                        : t.tm_hour < 22 ? "Good evening"
                                         : "Good night";
    const char *name = device_user_name();
    if (name[0] && strlen(hello) + strlen(name) + 3 <= 26)
        snprintf(m.greeting, sizeof(m.greeting), "%s, %s!", hello, name);
    else
        snprintf(m.greeting, sizeof(m.greeting), "%s!", hello);
    m.greet_icon = t.tm_hour >= 6 && t.tm_hour < 18 ? ICON_SUN : ICON_MOON;

    // Today's events (static: 2 KB is too much for the stack).
    // Today's holidays come first, in purple.
    static int idx[CAL_MAX];
    const char *hol[2];
    int nh = day_holidays(today, hol, 2);
    int nev = cal_events_on(&g_cal, today, idx, CAL_MAX);
    m.ev_total = nh + nev;
    m.ev_n = m.ev_total < SHOWN ? m.ev_total : SHOWN;
    for (int i = 0; i < m.ev_n; i++)
    {
        if (i < nh)
        {
            snprintf(m.ev_time[i], sizeof(m.ev_time[i]), "all");
            snprintf(m.ev_text[i], sizeof(m.ev_text[i]), "%s", hol[i]);
            m.ev_ink[i] = C_PURPLE;
            continue;
        }
        const CalEvent *e = &g_cal.ev[idx[i - nh]];
        cal_time_text(e, today, m.ev_time[i], sizeof(m.ev_time[i]));
        home_label(e, today, m.ev_text[i], sizeof(m.ev_text[i]));
        m.ev_ink[i] = event_ink(e);
    }
    // A holiday cheers the greeting ("Happy Thanksgiving!"), except the
    // clock changes, which are a reminder instead.
    if (nh > 0 && strncmp(hol[0], "Clocks", 6) != 0 && strlen(hol[0]) <= 19)
        snprintf(m.greeting, sizeof(m.greeting), "Happy %s!", hol[0]);
    // A birthday or anniversary today takes over the greeting.
    for (int i = 0; i < nev; i++)
    {
        const CalEvent *e = &g_cal.ev[idx[i]];
        char who[CAL_TEXT_MAX + 32];
        if (!cal_is_person(e))
            continue;
        cal_label(e, today, who, sizeof(who));
        if (strlen(who) <= 19)
        {
            snprintf(m.greeting, sizeof(m.greeting), "%s today!", who);
            m.greet_icon = e->repeat == 'A' ? ICON_HEART : ICON_CAKE;
            break;
        }
    }

    // To-dos for now: late first, then due today, then the undated ones.
    // Ones due later wait in the ticker and the Agenda.
    m.td_total = m.td_n = 0;
    m.td_urgent = false;
    m.td_open = todo_open_count(&g_todo);
    for (int pass = 0; pass < 3; pass++)
    {
        for (int i = 0; i < g_todo.count; i++)
        {
            const TodoItem *it = &g_todo.items[i];
            if (it->done)
                continue;
            int c = it->has_due ? date_cmp(it->due, today) : 2;   // <0 late, 0 today
            if (c == 1 || (pass == 0 && c >= 0) || (pass == 1 && c != 0) ||
                (pass == 2 && c != 2))
                continue;
            if (c <= 0)
                m.td_urgent = true;
            if (m.td_n < SHOWN)
            {
                snprintf(m.td_text[m.td_n], sizeof(m.td_text[0]), "%s", it->text);
                m.td_late[m.td_n] = c <= 0;
                m.td_n++;
            }
            m.td_total++;
        }
    }

    // ---- Ticker
    m.tick_n = 0;
    char line[96], label[CAL_TEXT_MAX + 32];
    m.habits_done = habit_done_count(&g_habits, today);
    if (g_habits.count > 0)
    {
        bool all = m.habits_done == g_habits.count;
        if (all)
            snprintf(line, sizeof(line), "All %d done today!", g_habits.count);
        else
            snprintf(line, sizeof(line), "%d of %d done today", m.habits_done, g_habits.count);
        add_tick("HABITS", SEC_HABITS, all ? C_OK : C_INK, line, J_HABITS, today);
    }
    char alarm[48];
    if (timers_next_alarm(now, alarm, sizeof(alarm)))
        add_tick("ALARM", SEC_TIMERS, C_INK, alarm, J_TIMERS, today);

    // Coming up: the next event (not a birthday, not a daily repeat), the
    // next birthday or anniversary, and the next to-do due soon.
    int ev_i = -1, bd_i = -1;
    for (int d = 1; d <= COMING_DAYS && (ev_i < 0 || bd_i < 0); d++)
    {
        Date day = date_add_days(today, d);
        int ne = cal_events_on(&g_cal, day, idx, CAL_MAX);
        for (int k = 0; k < ne; k++)
        {
            const CalEvent *e = &g_cal.ev[idx[k]];
            if (cal_span(e) > 0 && cal_day_index(e, day) != 0)
                continue;   // under way already: not "coming up"
            bool person = cal_is_person(e);
            if ((person && bd_i >= 0) || (!person && (ev_i >= 0 || e->repeat == 'D')))
                continue;
            if (person)
                bd_i = idx[k];
            else
                ev_i = idx[k];
            char when[8], tm[16];
            when_text(d, day, when, sizeof(when));
            home_label(e, day, label, sizeof(label));
            cal_time_text(e, day, tm, sizeof(tm));
            if (e->minute == CAL_ALL_DAY)
                snprintf(line, sizeof(line), "%s: %.40s", when, label);
            else
                snprintf(line, sizeof(line), "%s %s %.40s", when, tm, label);
            add_tick("SOON", person ? SEC_ALERT : SEC_CAL, event_ink(e), line, J_DAY, day);
        }
    }
    // The next holiday within two weeks (today's is already in TODAY).
    if (device_holidays())
    {
        Date hd;
        const char *hname;
        if (holiday_next(date_add_days(today, 1), 14, &hd, &hname))
        {
            char when[8];
            long in = date_to_days(hd) - date_to_days(today);
            when_text((int)in, hd, when, sizeof(when));
            snprintf(line, sizeof(line), "%s: %s", when, hname);
            add_tick("HOLIDAY", SEC_SETTINGS, C_PURPLE, line, J_DAY, hd);
        }
    }

    int td_i = -1, td_d = 0;
    Date tomorrow = date_add_days(today, 1);
    for (int i = 0; i < g_todo.count; i++)
    {
        if (g_todo.items[i].done)
            continue;
        uint32_t mask = todo_days_mask(&g_todo.items[i], tomorrow, TODO_AHEAD);
        int d = mask ? __builtin_ctz(mask) + 1 : 0;
        if (d && (td_i < 0 || d < td_d))
        {
            td_i = i;
            td_d = d;
        }
    }
    if (td_i >= 0)
    {
        char when[8];
        when_text(td_d, date_add_days(today, td_d), when, sizeof(when));
        snprintf(line, sizeof(line), "%s: %.40s", when, g_todo.items[td_i].text);
        add_tick("TO-DO", SEC_TODO, C_INK, line, J_TODO, today);
    }
}

static void ensure(void)
{
    if (!m.valid || m.minute != (long)(ui_now() / 60))
        build();
}

// The ticker turns every TICK_FRAMES; L and R turn it by hand.
static int tick_cur = 0, tick_timer = 0;

static int tick_index(void)
{
    return m.tick_n > 0 ? ((tick_cur % m.tick_n) + m.tick_n) % m.tick_n : 0;
}

// SELECT: the page for what the ticker shows.
static void tick_jump(void)
{
    if (m.tick_n == 0)
        return;
    const Tick *t = &m.tick[tick_index()];
    switch (t->jump)
    {
        case J_HABITS: ui_push(&SCR_HABITS); break;
        case J_TIMERS: ui_push(&SCR_TIMERS); break;
        case J_TODO: ui_push(&SCR_TODO); break;
        case J_DAY:
            calendar_show_day(t->day);
            ui_push(&SCR_CALENDAR);
            break;
    }
}

// ---------------------------------------------------------------- top

static void draw_top(Surface *s)
{
    ensure();
    time_t now = ui_now();
    struct tm t;
    localtime_r(&now, &t);
    char buf[64];

    gfx_header(s, "", NULL);
    gfx_icon(s, 4, 1, m.greet_icon, 1);
    gfx_text_n(s, 24, 5, m.greeting, 26, C_GOLD);
    gfx_paper(s, PAPER_Y);

    // Masthead on plain paper: the date block and the clock.
    gfx_fill(s, 17, PAPER_Y, SCREEN_W - 17, 4 * PAPER_ROW - 1, C_PAPER);
    date_block_draw(s, 22, 21, (Date){ t.tm_year + 1900, t.tm_mon + 1, t.tm_mday });

    // 12-hour clock: "1:30" big, "PM" small beside it.
    int h12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    snprintf(buf, sizeof(buf), "%d:%02d", h12, t.tm_min);
    int end = gfx_text_scaled(s, 74, 22, buf, C_INK, 3);
    gfx_text(s, end + 2, 37, t.tm_hour >= 12 ? "PM" : "AM", C_INK_SOFT);
    snprintf(buf, sizeof(buf), "%s, %s %d", DATE_DAY[t.tm_wday % 7], DATE_MONTH[t.tm_mon % 12],
             t.tm_mday);
    if (strlen(buf) > 22)
        snprintf(buf, sizeof(buf), "%s, %.3s %d", DATE_DAY[t.tm_wday % 7],
                 DATE_MONTH[t.tm_mon % 12], t.tm_mday);
    gfx_text(s, 74, 52, buf, C_INK_SOFT);

    int row = 4;   // next ruled row; everything below sits on its line
#define BAND (PAPER_Y + row * PAPER_ROW)
#define LINE_Y (PAPER_TEXT(row))

    // Missed alarms and storage or clock problems first, in red.
    int alerts = 0;
    for (int i = 0; i < missed_count() && alerts < 2; i++, alerts++, row++)
    {
        snprintf(buf, sizeof(buf), "Missed %s", missed_text(i));
        gfx_text_n(s, 20, LINE_Y, buf, 29, C_ALERT);
    }
    if (!app_storage_ok() && alerts < 3)
    {
        gfx_text(s, 20, LINE_Y, "No SD card. Not saving.", C_ALERT);
        alerts++, row++;
    }
    else if (app_last_error()[0] && alerts < 3)
    {
        gfx_text_n(s, 20, LINE_Y, app_last_error(), 29, C_ALERT);
        alerts++, row++;
    }
    // A DSi whose clock battery ran down restarts in 2000; alarms, due dates
    // and file times are then all wrong.
    if (t.tm_year + 1900 < 2024 && alerts < 3)
    {
        gfx_text(s, 20, LINE_Y, "Set clock in DSi Settings.", C_ALERT);
        alerts++, row++;
    }

    // ---- TODAY
    int x = gfx_chip(s, 20, BAND, "TODAY", SEC_MID(SEC_CAL), C_SNOW);
    if (m.ev_total > m.ev_n)
    {
        snprintf(buf, sizeof(buf), "+%d more", m.ev_total - m.ev_n);
        gfx_text(s, x + 6, LINE_Y, buf, C_INK_SOFT);
    }
    row++;
    if (m.ev_total == 0)
    {
        gfx_text(s, 28, LINE_Y, "No plans.", C_INK_SOFT);
        row++;
    }
    for (int i = 0; i < m.ev_n; i++, row++)
    {
        // A time range ("1:30p-5:30p") is wider than the time column: the
        // text then starts after it instead of over it.
        int tx = gfx_text(s, 28, LINE_Y, m.ev_time[i], C_ACCENT) + 8;
        if (tx < 80)
            tx = 80;
        gfx_text_n(s, tx, LINE_Y, m.ev_text[i], (SCREEN_W - 6 - tx) / FONT_W, m.ev_ink[i]);
    }

    // ---- TO-DO: as many as fit above the ticker line.
    x = gfx_chip(s, 20, BAND, "TO-DO", SEC_MID(SEC_TODO), C_INK);
    int room = PAPER_ROWS - 1 - (row + 1);
    int shown = m.td_n < room ? m.td_n : room;
    if (m.td_total == 0)
        gfx_text(s, x + 6, LINE_Y, m.td_open ? "None due." : "All clear.", C_OK);
    else if (m.td_total > shown)
    {
        snprintf(buf, sizeof(buf), "+%d more", m.td_total - shown);
        gfx_text(s, x + 6, LINE_Y, buf, C_INK_SOFT);
    }
    row++;
    for (int i = 0; i < shown; i++, row++)
    {
        gfx_panel(s, 28, BAND + 1, 10, 10, 1, C_WHITE, m.td_late[i] ? C_ALERT : C_INK_SOFT);
        gfx_text_n(s, 43, LINE_Y, m.td_text[i], 26, m.td_late[i] ? C_ALERT : C_INK);
    }

    // ---- Ticker on the last line.
    if (m.tick_n > 0)
    {
        row = PAPER_ROWS - 1;
        int ti = tick_index();
        const Tick *tk = &m.tick[ti];
        x = gfx_chip(s, 20, BAND, tk->tag, SEC_MID(tk->sec),
                     tk->sec == SEC_ALERT || tk->sec == SEC_CAL ? C_SNOW : C_INK);
        // The whole rest of the line is the text's.
        gfx_text_n(s, x + 5, LINE_Y, tk->text, (SCREEN_W - 4 - x - 5) / FONT_W, tk->ink);
    }
#undef BAND
#undef LINE_Y
}

// ---------------------------------------------------------------- bottom

#define TILES 6
static const struct {
    const char *label;
    int icon, sec;
    const Screen *target;
} TILE[TILES] = {
    { "To-Do", ICON_TODO, SEC_TODO, &SCR_TODO },
    { "Notes", ICON_NOTES, SEC_NOTES, &SCR_NOTES },
    { "Calendar", ICON_CAL_BLANK, SEC_CAL, &SCR_CALENDAR },
    { "Habits", ICON_HABITS, SEC_HABITS, &SCR_HABITS },
    { "Timers", ICON_TIMERS, SEC_TIMERS, &SCR_TIMERS },
    { "Games", ICON_GAMES, SEC_GAMES, &SCR_LAUNCH },
};

static Press press;
static int sel = 0;
static bool cursor = false;   // the D-pad cursor shows

static void tile_rects(Rect *r)
{
    for (int i = 0; i < TILES; i++)
        r[i] = (Rect){ (short)(5 + (i % 3) * 83), (short)(29 + (i / 3) * 81), 80, 76 };
}

// The soonest running countdown as "4:32", or "" when none runs.
static void timer_badge(char *out, int size)
{
    out[0] = '\0';
    int best = -1;
    for (int i = 0; i < TIMER_MAX; i++)
    {
        if (!g_timers[i].used || !g_timers[i].running)
            continue;
        int left = timers_left(&g_timers[i], ui_now());
        if (best < 0 || left < best)
            best = left;
    }
    if (best >= 0)
        snprintf(out, (size_t)size, "%d:%02d", best / 60, best % 60);
}

static void draw_bottom(Surface *s)
{
    ensure();
    gfx_page(s, UI_HEAD_H);
    Rect r[TILES];
    tile_rects(r);
    time_t now = ui_now();
    struct tm t;
    localtime_r(&now, &t);
    for (int i = 0; i < TILES; i++)
    {
        bool down = press_is(&press, i);
        int x = r[i].x, y = r[i].y, w = r[i].w, h = r[i].h;
        if (!down)
            gfx_round_fill(s, x + 2, y + 3, w, h, 3, C_SHADOW);
        gfx_button(s, x, y, w, h, NULL, ICON_NONE, TILE[i].sec, down, true);
        int top = y + (down ? 2 : 0);
        int ix = x + w / 2 - 16, iy = top + 12;
        gfx_icon(s, ix, iy, TILE[i].icon, 2);
        gfx_text_center(s, x + w / 2, iy + 39, TILE[i].label, C_INK);
        if (i == 2)
        {
            // Today's date on the calendar page.
            char day[4];
            snprintf(day, sizeof(day), "%d", t.tm_mday);
            gfx_text_center(s, x + w / 2, iy + 14, day, C_INK);
        }

        char badge[24] = "";
        uint8_t face = SEC_DK(TILE[i].sec);
        if (i == 0 && m.td_open > 0)
        {
            snprintf(badge, sizeof(badge), "%d", m.td_open);
            if (m.td_urgent)
                face = SEC_MID(SEC_ALERT);
        }
        else if (i == 3 && g_habits.count > 0)
        {
            snprintf(badge, sizeof(badge), "%d/%d", m.habits_done, g_habits.count);
        }
        else if (i == 4)
        {
            timer_badge(badge, sizeof(badge));
        }
        if (badge[0])
        {
            int bw = (int)strlen(badge) * FONT_W + 7;
            gfx_chip(s, x + w - bw - 3, top + 4, badge, face, C_SNOW);
        }
        if (cursor && i == sel)
        {
            gfx_frame(s, x - 2, y - 2, w + 4, h + 5, C_ACCENT);
            gfx_frame(s, x - 3, y - 3, w + 6, h + 7, C_ACCENT);
            gfx_pad_badge(s, x + 3, y + 3, 'A');
        }
    }
}

static void open_tile(int i)
{
    if (TILE[i].target == &SCR_LAUNCH)
        launch_select_games();
    ui_push(TILE[i].target);
}

static void update(const Input *in)
{
    Rect r[TILES];
    tile_rects(r);
    int fired = press_update(&press, in, r, TILES);
    if (fired >= 0)
    {
        sel = fired;
        open_tile(fired);
        return;
    }

    // Buttons: the D-pad moves a cursor over the tiles and A opens one. The
    // first press only shows the cursor.
    if (!ui_pad_mode())
        cursor = false;
    uint32_t dpad = in->repeat & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
    if ((dpad || (in->down & KEY_A)) && !cursor)
    {
        cursor = true;
        ui_dirty_bottom();
    }
    else if (dpad)
    {
        int col = sel % 3, row = sel / 3;
        if ((dpad & KEY_LEFT) && col > 0)
            col--;
        if ((dpad & KEY_RIGHT) && col < 2)
            col++;
        if (dpad & (KEY_UP | KEY_DOWN))
            row = (dpad & KEY_UP) ? 0 : 1;
        sel = row * 3 + col;
        ui_dirty_bottom();
    }
    else if (in->down & KEY_A)
    {
        open_tile(sel);
        return;
    }
    // START goes for the games: START again launches TWiLight Menu++.
    if (in->down & KEY_START)
    {
        launch_select_games();
        ui_push(&SCR_LAUNCH);
        return;
    }

    // The ticker: L and R step it, SELECT opens what it shows; otherwise it
    // moves on by itself. A running countdown counts on its tile.
    if (in->down & KEY_SELECT)
    {
        tick_jump();
        return;
    }
    if (in->repeat & (KEY_L | KEY_R))
    {
        tick_cur += (in->repeat & KEY_R) ? 1 : -1;
        tick_timer = 0;
        sfx_play(SFX_TAP);
    }
    else if (++tick_timer >= TICK_FRAMES)
    {
        tick_cur++;
        tick_timer = 0;
    }
    static int tick_shown = -1;
    static time_t second_shown;
    if (tick_index() != tick_shown)
    {
        tick_shown = tick_index();
        ui_dirty_top();
    }
    if (ui_now() != second_shown)
    {
        second_shown = ui_now();
        char b[12];
        timer_badge(b, sizeof(b));
        if (b[0])
            ui_dirty_bottom();
    }
}

static const char *title(void)
{
    static char t[32];
    const char *name = device_user_name();
    if (name[0] && strlen(name) <= 10)
        snprintf(t, sizeof(t), "%s's planner", name);
    else
        snprintf(t, sizeof(t), "My planner");
    return t;
}

static void open_settings(void)
{
    ui_push(&SCR_SETTINGS);
}

static void enter(void)
{
    m.valid = false;   // to-dos, events or habits may have changed
}

const Screen SCR_HOME = {
    .enter = enter,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title_fn = title,
    .sec = SEC_PLAIN,
    .on_more = open_settings,
    .more_icon = ICON_GEAR,
};
