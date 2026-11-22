// Host-side tests for the PDA file formats. Build and run with tests/host/run.sh.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../source/input/hwr.h"
#include "../../source/input/layout.h"
#include "../../source/input/swipe.h"
#include "../../source/store/alarm_io.h"
#include "../../source/store/bmp_io.h"
#include "../../source/store/habit_io.h"
#include "../../source/store/holidays.h"
#include "../../source/store/cal_io.h"
#include "../../source/store/date.h"
#include "../../source/store/ini.h"
#include "../../source/store/notes_io.h"
#include "../../source/store/textio.h"
#include "../../source/store/todo_io.h"
#include "../../source/store/work.h"

static int failures = 0, checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

static char tmpdir[256];

static void tmp_path(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s/%s", tmpdir, name);
}

static Date D(int y, int m, int d)
{
    Date x = { y, m, d };
    return x;
}

// ---------------------------------------------------------------- date

static void test_date(void)
{
    CHECK(date_is_leap(2024) && !date_is_leap(2026) && date_is_leap(2000) && !date_is_leap(1900));
    CHECK(date_days_in_month(2026, 2) == 28 && date_days_in_month(2028, 2) == 29);
    CHECK(date_weekday(D(2026, 11, 20)) == 5);  // Friday
    CHECK(date_weekday(D(1970, 1, 1)) == 4);
    CHECK(date_weekday(D(2000, 1, 1)) == 6);
    for (long n = -1000; n < 40000; n += 37)
    {
        Date d = date_from_days(n);
        CHECK(date_to_days(d) == n);
    }
    Date d;
    CHECK(date_parse("2026-02-28", &d) && d.y == 2026 && d.m == 2 && d.d == 28);
    CHECK(!date_parse("2026-02-29", &d));
    CHECK(!date_parse("2026-2-28", &d));
    CHECK(!date_parse("2026-02", &d));
    CHECK(!date_parse("", &d));
    char buf[11];
    date_format(D(2026, 1, 5), buf);
    CHECK(strcmp(buf, "2026-01-05") == 0);
    CHECK(time_parse_hhmm("07:30") == 450);
    CHECK(time_parse_hhmm("24:00") == -1);
    CHECK(time_parse_hhmm("7:3") == -1);
    CHECK(time_parse_hhmm("12") == -1);
    Date e = date_add_days(D(2026, 12, 31), 1);
    CHECK(e.y == 2027 && e.m == 1 && e.d == 1);
}

static bool rel_is(const char *s, Date today, int y, int m, int d)
{
    Date out;
    return date_parse_relative(s, today, &out) && out.y == y && out.m == m && out.d == d;
}

static void test_date_relative(void)
{
    Date fri = D(2026, 11, 20);  // a Friday
    Date out;
    CHECK(rel_is("today", fri, 2026, 11, 20));
    CHECK(rel_is("TOD", fri, 2026, 11, 20));
    CHECK(rel_is("tmrw", fri, 2026, 11, 21));
    CHECK(rel_is("Tomorrow", fri, 2026, 11, 21));
    CHECK(rel_is("tom", fri, 2026, 11, 21));
    CHECK(rel_is("sat", fri, 2026, 11, 21));
    CHECK(rel_is("monday", fri, 2026, 11, 23));
    CHECK(rel_is("thurs", fri, 2026, 11, 26));
    CHECK(rel_is("fri", fri, 2026, 11, 27));      // a week ahead, not today
    CHECK(rel_is("+3", fri, 2026, 11, 23));
    CHECK(rel_is("+0", fri, 2026, 11, 20));
    CHECK(rel_is("+2w", fri, 2026, 12, 4));
    CHECK(rel_is("+45d", fri, 2027, 1, 4));
    CHECK(rel_is("12-24", fri, 2026, 12, 24));
    CHECK(rel_is("1/5", fri, 2027, 1, 5));         // already past: next year
    CHECK(rel_is("11-20", fri, 2026, 11, 20));     // today counts
    CHECK(rel_is("2-29", fri, 2028, 2, 29));       // next leap year
    CHECK(rel_is("2027-03-01", fri, 2027, 3, 1));
    CHECK(rel_is("sat", D(2026, 12, 31), 2027, 1, 2));
    CHECK(!date_parse_relative("", fri, &out));
    CHECK(!date_parse_relative("fr", fri, &out));
    CHECK(rel_is("frid", fri, 2026, 11, 27));      // any 3+ letter prefix works
    CHECK(!date_parse_relative("fridays", fri, &out));
    CHECK(!date_parse_relative("+", fri, &out));
    CHECK(!date_parse_relative("+3x", fri, &out));
    CHECK(!date_parse_relative("+1000", fri, &out));
    CHECK(!date_parse_relative("13-01", fri, &out));
    CHECK(!date_parse_relative("2-30", fri, &out));
    CHECK(!date_parse_relative("soon", fri, &out));
    CHECK(!date_parse_relative("a-very-long-word-here", fri, &out));
}

// ---------------------------------------------------------------- todo

static char *roundtrip_todo(const TodoList *l)
{
    TextBuf b;
    textbuf_init(&b);
    CHECK(todo_serialize(l, &b));
    return b.data;
}

static void test_todo(void)
{
    static TodoList l, l2;

    char empty[] = "";
    todo_parse(&l, empty);
    CHECK(l.count == 0);

    char crlf[] = "# comment\r\n[ ] Buy milk\r\n[x] Call bank due:2026-11-21\r\n\r\n"
                  "plain line\r\n[X] Done upper\r\n[ ]\r\n";
    todo_parse(&l, crlf);
    CHECK(l.count == 4);
    CHECK(!l.items[0].done && strcmp(l.items[0].text, "Buy milk") == 0);
    CHECK(l.items[1].done && l.items[1].has_due && l.items[1].due.d == 21);
    CHECK(strcmp(l.items[1].text, "Call bank") == 0);
    CHECK(!l.items[2].done && strcmp(l.items[2].text, "plain line") == 0);
    CHECK(l.items[3].done);

    char *out = roundtrip_todo(&l);
    CHECK(strstr(out, "[x] Call bank due:2026-11-21\n") != NULL);
    todo_parse(&l2, out);
    CHECK(l2.count == l.count);
    for (int i = 0; i < l.count; i++)
    {
        CHECK(l2.items[i].done == l.items[i].done);
        CHECK(strcmp(l2.items[i].text, l.items[i].text) == 0);
        CHECK(l2.items[i].has_due == l.items[i].has_due);
    }
    free(out);

    // A file ending in a lone "[" (no newline): the checkbox test must not
    // read past the end of the buffer. Exactly sized so ASan sees it.
    char *bracket = malloc(2);
    memcpy(bracket, "[", 2);
    todo_parse(&l, bracket);
    CHECK(l.count == 1 && strcmp(l.items[0].text, "[") == 0 && !l.items[0].done);
    free(bracket);
    char *open = malloc(3);
    memcpy(open, "[x", 3);
    todo_parse(&l, open);
    CHECK(l.count == 1 && strcmp(l.items[0].text, "[x") == 0);
    free(open);

    // Bad due date stays in the text.
    todo_clear(&l);
    CHECK(todo_add(&l, "Pay due:2026-13-01") == 0);
    CHECK(!l.items[0].has_due && strcmp(l.items[0].text, "Pay due:2026-13-01") == 0);

    // 120-char lines and truncation.
    char longline[300];
    memset(longline, 'a', 250);
    longline[250] = '\0';
    todo_clear(&l);
    CHECK(todo_add(&l, longline) == 0);
    CHECK(strlen(l.items[0].text) == TODO_TEXT_MAX);
    CHECK(todo_add(&l, "   ") == -1);

    // 200 items round trip; TODO_MAX cap.
    todo_clear(&l);
    for (int i = 0; i < 200; i++)
    {
        char t[32];
        snprintf(t, sizeof(t), "Item %d", i);
        CHECK(todo_add(&l, t) == i);
    }
    l.items[7].done = true;
    out = roundtrip_todo(&l);
    todo_parse(&l2, out);
    CHECK(l2.count == 200 && l2.items[7].done && strcmp(l2.items[199].text, "Item 199") == 0);
    free(out);
    for (int i = 200; i < TODO_MAX + 5; i++)
        todo_add(&l, "more");
    CHECK(l.count == TODO_MAX);

    // Move and remove.
    todo_clear(&l);
    todo_add(&l, "A");
    todo_add(&l, "B");
    todo_add(&l, "C");
    CHECK(todo_move(&l, 0, +1) == 1 && strcmp(l.items[0].text, "B") == 0);
    CHECK(todo_move(&l, 0, -1) == 0);
    todo_remove(&l, 1);
    CHECK(l.count == 2 && strcmp(l.items[1].text, "C") == 0);
    CHECK(todo_open_count(&l) == 2);

    // Relative due dates typed in the editor.
    Date fri = D(2026, 11, 20);
    char t[TODO_TEXT_MAX + 40];
    snprintf(t, sizeof(t), "Pay rent due:fri  ");
    CHECK(todo_expand_due(t, sizeof(t), fri) == 1 &&
          strcmp(t, "Pay rent due:2026-11-27") == 0);
    snprintf(t, sizeof(t), "due:TMRW");
    CHECK(todo_expand_due(t, sizeof(t), fri) == 1 && strcmp(t, "due:2026-11-21") == 0);
    snprintf(t, sizeof(t), "Call due:2026-12-01");
    CHECK(todo_expand_due(t, sizeof(t), fri) == 1 && strcmp(t, "Call due:2026-12-01") == 0);
    snprintf(t, sizeof(t), "Call due:soon");
    CHECK(todo_expand_due(t, sizeof(t), fri) == -1 && strcmp(t, "Call due:soon") == 0);
    snprintf(t, sizeof(t), "No date here");
    CHECK(todo_expand_due(t, sizeof(t), fri) == 0 && strcmp(t, "No date here") == 0);
    snprintf(t, sizeof(t), "Due: is just a word");
    CHECK(todo_expand_due(t, sizeof(t), fri) == 0);
    char small[12] = "x due:+3";
    CHECK(todo_expand_due(small, sizeof(small), fri) == -1);  // no room to expand
    snprintf(t, sizeof(t), "Gym due:mon");
    todo_expand_due(t, sizeof(t), fri);
    todo_clear(&l);
    CHECK(todo_add(&l, t) == 0 && l.items[0].has_due && l.items[0].due.d == 23 &&
          strcmp(l.items[0].text, "Gym") == 0);
}

static void test_todo_repeat(void)
{
    static TodoList l, l2;
    Date fri = D(2026, 11, 20);
    char text[] = "[ ] Water plants due:2026-11-23 every:3d\n"
                  "[ ] Rent every:m due:2026-12-01\n"
                  "[ ] Gym due:2026-11-16 every:week\n"
                  "[ ] Someday every:2w\n"
                  "[ ] Odd every:x\n";
    todo_parse(&l, text);
    CHECK(l.count == 5);
    CHECK(l.items[0].repeat == 'd' && l.items[0].every == 3 && l.items[0].due.d == 23);
    CHECK(strcmp(l.items[0].text, "Water plants") == 0);
    CHECK(l.items[1].repeat == 'm' && l.items[1].has_due && l.items[1].due.m == 12);
    CHECK(strcmp(l.items[1].text, "Rent") == 0);
    CHECK(l.items[2].repeat == 'w' && l.items[2].every == 1);
    CHECK(l.items[3].repeat == 'w' && l.items[3].every == 2 && !l.items[3].has_due);
    CHECK(l.items[4].repeat == 0 && strcmp(l.items[4].text, "Odd every:x") == 0);

    // Checking moves the due date past today and keeps the item open.
    CHECK(todo_complete(&l.items[0], fri) && !l.items[0].done && l.items[0].due.d == 26);
    CHECK(todo_complete(&l.items[2], fri) && l.items[2].due.d == 23);   // late: next Mon
    CHECK(todo_complete(&l.items[3], fri) && l.items[3].has_due &&
          l.items[3].due.m == 12 && l.items[3].due.d == 4);             // no date: from today
    CHECK(!todo_complete(&l.items[4], fri) && l.items[4].done);

    // Round trip.
    TextBuf b;
    textbuf_init(&b);
    CHECK(todo_serialize(&l, &b));
    CHECK(strstr(b.data, "[ ] Water plants due:2026-11-26 every:3d\n") != NULL);
    CHECK(strstr(b.data, "[ ] Rent due:2026-12-01 every:m\n") != NULL);
    todo_parse(&l2, b.data);
    CHECK(l2.count == 5 && l2.items[0].every == 3 && l2.items[1].repeat == 'm');
    textbuf_free(&b);

    // Editing the text keeps the date, the repeat and the check unless new
    // ones are typed; an edit that leaves no text changes nothing.
    TodoItem e;
    todo_set_text(&e, "Water plants due:2026-11-23 every:3d");
    e.done = true;
    CHECK(todo_edit_text(&e, "Water the plants") && strcmp(e.text, "Water the plants") == 0);
    CHECK(e.done && e.has_due && e.due.d == 23 && e.repeat == 'd' && e.every == 3);
    CHECK(todo_edit_text(&e, "Water them due:2026-12-01") && e.due.m == 12 && e.due.d == 1 &&
          e.repeat == 'd' && e.every == 3);
    CHECK(todo_edit_text(&e, "Water them every:2w") && e.repeat == 'w' && e.every == 2 &&
          e.due.m == 12 && e.due.d == 1);
    CHECK(!todo_edit_text(&e, "   ") && strcmp(e.text, "Water them") == 0 && e.repeat == 'w');
    CHECK(!todo_edit_text(&e, "due:2027-01-01") && e.due.m == 12);

    // Months clamp to the month's length; years keep Feb 29 on Feb 28.
    Date d = todo_next_due(D(2026, 1, 31), 'm', 1);
    CHECK(d.m == 2 && d.d == 28);
    d = todo_next_due(D(2028, 2, 29), 'y', 1);
    CHECK(d.y == 2029 && d.m == 2 && d.d == 28);
    d = todo_next_due(D(2026, 11, 20), 'm', 3);
    CHECK(d.y == 2027 && d.m == 2 && d.d == 20);
    d = date_add_months(D(2026, 3, 31), -1);
    CHECK(d.m == 2 && d.d == 28);

    char u;
    int n;
    CHECK(todo_parse_every("2w", &u, &n) && u == 'w' && n == 2);
    CHECK(todo_parse_every("monthly", &u, &n) && u == 'm' && n == 1);
    CHECK(todo_parse_every("Daily", &u, &n) && u == 'd');
    CHECK(!todo_parse_every("0d", &u, &n) && !todo_parse_every("", &u, &n) &&
          !todo_parse_every("3", &u, &n) && !todo_parse_every("w2", &u, &n));
    char s[32];
    todo_repeat_text('w', 2, s, sizeof(s));
    CHECK(strcmp(s, "every 2 weeks") == 0);
    todo_repeat_text('d', 1, s, sizeof(s));
    CHECK(strcmp(s, "every day") == 0);

    // Days in a window: the due date and each repeat.
    TodoItem w = { 0 };
    todo_set_text(&w, "Plants due:2026-11-24 every:3d");
    CHECK(todo_days_mask(&w, fri, 14) == ((1u << 4) | (1u << 7) | (1u << 10) | (1u << 13)));
    todo_set_text(&w, "Gym due:2026-10-05 every:w");      // a Monday, long ago
    CHECK(todo_days_mask(&w, fri, 10) == ((1u << 3)));    // Mon 23 Nov
    todo_set_text(&w, "Rent due:2026-01-31 every:m");     // clamps: ..Nov 28?
    uint32_t mm = todo_days_mask(&w, D(2026, 11, 1), 30);
    CHECK(mm != 0 && __builtin_popcount(mm) == 1);
    todo_set_text(&w, "Once due:2026-11-21");
    CHECK(todo_days_mask(&w, fri, 7) == (1u << 1));
    CHECK(todo_days_mask(&w, D(2026, 11, 22), 7) == 0);    // before the window
    todo_set_text(&w, "No date");
    CHECK(todo_days_mask(&w, fri, 30) == 0);

    // Sort: dated by date, then undated, then checked; stable inside groups.
    static TodoList sl;
    char st[] = "[ ] b\n[x] done1 due:2026-11-01\n[ ] late due:2026-11-02\n"
                "[ ] a\n[ ] soon due:2026-11-21\n[x] done2\n[ ] first due:2026-10-01\n";
    todo_parse(&sl, st);
    todo_sort_by_due(&sl);
    const char *order[] = { "first", "late", "soon", "b", "a", "done1", "done2" };
    for (int k = 0; k < 7; k++)
        CHECK(strcmp(sl.items[k].text, order[k]) == 0);

    // A typed relative date followed by a repeat.
    char t[TODO_TEXT_MAX + 40];
    snprintf(t, sizeof(t), "Gym due:mon every:w");
    CHECK(todo_expand_due(t, sizeof(t), fri) == 1 &&
          strcmp(t, "Gym due:2026-11-23 every:w") == 0);
}

// ---------------------------------------------------------------- calendar

static void test_cal(void)
{
    static CalList c;
    char text[] = "# header\n"
                  "2026-11-20|14:00|-|Dentist\n"
                  "2024-02-29|--:--|Y|Leap birthday\n"
                  "2026-01-31|09:00|M|Pay rent\n"
                  "2026-12-28|08:00|W|Gym\n"
                  "2026-11-01|07:00|d|Lowercase daily\n"
                  "bad line\n"
                  "2026-11-20|25:00|-|Bad time\n"
                  "2026-11-20|10:00|Q|Bad repeat\n"
                  "2026-11-20|10:00|-|Pipe | in text\n";
    cal_parse(&c, text);
    CHECK(c.count == 6);
    CHECK(c.ev[4].repeat == 'D');
    CHECK(strcmp(c.ev[5].text, "Pipe | in text") == 0);

    // Once.
    CHECK(cal_occurs_on(&c.ev[0], D(2026, 11, 20)));
    CHECK(!cal_occurs_on(&c.ev[0], D(2026, 11, 21)));
    // Yearly Feb 29: only leap years.
    CHECK(cal_occurs_on(&c.ev[1], D(2028, 2, 29)));
    CHECK(!cal_occurs_on(&c.ev[1], D(2027, 2, 28)));
    CHECK(!cal_occurs_on(&c.ev[1], D(2023, 2, 28)));
    // Monthly on the 31st skips short months.
    CHECK(cal_occurs_on(&c.ev[2], D(2026, 3, 31)));
    CHECK(!cal_occurs_on(&c.ev[2], D(2026, 2, 28)));
    CHECK(!cal_occurs_on(&c.ev[2], D(2026, 4, 30)));
    CHECK(!cal_occurs_on(&c.ev[2], D(2025, 12, 31)));  // before start
    // Weekly across a year end.
    CHECK(cal_occurs_on(&c.ev[3], D(2027, 1, 4)));
    CHECK(cal_occurs_on(&c.ev[3], D(2027, 1, 11)));
    CHECK(!cal_occurs_on(&c.ev[3], D(2027, 1, 5)));
    CHECK(!cal_occurs_on(&c.ev[3], D(2026, 12, 21)));

    int idx[16];
    int n = cal_events_on(&c, D(2026, 11, 20), idx, 16);
    CHECK(n == 3);                      // Dentist, daily, pipe
    CHECK(c.ev[idx[0]].minute == 7 * 60);
    CHECK(c.ev[idx[1]].minute == 10 * 60);
    CHECK(c.ev[idx[2]].minute == 14 * 60);

    TextBuf b;
    textbuf_init(&b);
    CHECK(cal_serialize(&c, &b));
    CHECK(strstr(b.data, "2024-02-29|--:--|Y|Leap birthday\n") != NULL);
    CHECK(strstr(b.data, "2026-11-20|10:00|-|Pipe | in text\n") != NULL);
    static CalList c2;
    cal_parse(&c2, b.data);
    CHECK(c2.count == c.count);
    textbuf_free(&b);

    int minute, end_min;
    char t[CAL_TEXT_MAX + 1];
    CHECK(cal_parse_entry("14:30 Dentist", &minute, &end_min, t, sizeof(t)) && minute == 870 &&
          end_min == CAL_NO_END && strcmp(t, "Dentist") == 0);
    CHECK(cal_parse_entry("1:30-5:30 Workshop", &minute, &end_min, t, sizeof(t)) &&
          minute == 90 && end_min == 330 && strcmp(t, "Workshop") == 0);
    CHECK(cal_parse_entry("13:30-17:30 Class", &minute, &end_min, t, sizeof(t)) &&
          minute == 810 && end_min == 1050);
    CHECK(cal_parse_entry("9:00-x Odd", &minute, &end_min, t, sizeof(t)) &&
          minute == CAL_ALL_DAY && strcmp(t, "9:00-x Odd") == 0);
    // 12-hour input.
    CHECK(cal_parse_entry("1:30p-5:30pm Class", &minute, &end_min, t, sizeof(t)) &&
          minute == 810 && end_min == 1050 && strcmp(t, "Class") == 0);
    CHECK(cal_parse_entry("5p Dinner", &minute, &end_min, t, sizeof(t)) && minute == 1020);
    CHECK(cal_parse_entry("12a Midnight", &minute, &end_min, t, sizeof(t)) && minute == 0);
    CHECK(cal_parse_entry("12pm Lunch", &minute, &end_min, t, sizeof(t)) && minute == 720);
    CHECK(cal_parse_entry("10am-2pm Brunch", &minute, &end_min, t, sizeof(t)) &&
          minute == 600 && end_min == 840);
    CHECK(cal_parse_entry("2 eggs", &minute, &end_min, t, sizeof(t)) &&
          minute == CAL_ALL_DAY && strcmp(t, "2 eggs") == 0);
    CHECK(cal_parse_entry("13p Odd", &minute, &end_min, t, sizeof(t)) && minute == CAL_ALL_DAY);
    char tf[16];
    time_fmt12(0, TIME_SHORT, tf, sizeof(tf));
    CHECK(strcmp(tf, "12a") == 0);
    time_fmt12(720, TIME_SHORT, tf, sizeof(tf));
    CHECK(strcmp(tf, "12p") == 0);
    time_fmt12(810, TIME_SHORT, tf, sizeof(tf));
    CHECK(strcmp(tf, "1:30p") == 0);
    time_fmt12(1140, TIME_MIN, tf, sizeof(tf));
    CHECK(strcmp(tf, "7:00p") == 0);
    time_fmt12(450, TIME_LONG, tf, sizeof(tf));
    CHECK(strcmp(tf, "7:30 AM") == 0);
    CHECK(cal_parse_entry("9:05 Bus", &minute, &end_min, t, sizeof(t)) && minute == 545);
    CHECK(cal_parse_entry("Birthday", &minute, &end_min, t, sizeof(t)) && minute == CAL_ALL_DAY);
    CHECK(cal_parse_entry("12:", &minute, &end_min, t, sizeof(t)) && minute == CAL_ALL_DAY &&
          strcmp(t, "12:") == 0);
    CHECK(cal_parse_entry("1:", &minute, &end_min, t, sizeof(t)) && strcmp(t, "1:") == 0);
    CHECK(!cal_parse_entry("10:00", &minute, &end_min, t, sizeof(t)));
    CHECK(!cal_parse_entry("   ", &minute, &end_min, t, sizeof(t)));
    CHECK(cal_next_repeat('Y') == '-' && cal_next_repeat('-') == 'D');
}

static void test_work(void)
{
    Date mon = D(2026, 11, 23), sat = D(2026, 11, 21), sun = D(2026, 11, 22);
    int s, e;

    // The shifts as they are on the owner's card.
    static ShiftList l, l2;
    char text[] = "# comment\n"
                  "Bad|25:00-26:00\n"
                  "No hours|\n"
                  "|09:00-17:00\n"
                  "Early shift|10:00-16:30\n"
                  "Late shift|12:30-19:00|11:00-17:30\n"
                  "Early breaker|11:00-15:00\n"
                  "Middle breaker|12:00-16:00|10:30-14:30\n"
                  "Late breaker|13:30-17:30|12:00-16:00\n"
                  "Closer|12:00-19:00|10:30-17:30\n"
                  "Manager|08:00-17:00\n";
    shift_parse(&l, text);
    CHECK(l.count == SHIFT_MAX);
    struct { int wd_s, wd_e, we_s, we_e; } want[] = {
        { 600, 990, 600, 990 },     // 10a-4:30p
        { 750, 1140, 660, 1050 },   // 12:30p-7p / 11a-5:30p
        { 660, 900, 660, 900 },     // 11a-3p
        { 720, 960, 630, 870 },     // 12p-4p / 10:30a-2:30p
        { 810, 1050, 720, 960 },    // 1:30p-5:30p / 12p-4p
        { 720, 1140, 630, 1050 },   // 12p-7p / 10:30a-5:30p
        { 480, 1020, 480, 1020 },   // 8a-5p
    };
    for (int i = 0; i < SHIFT_MAX; i++)
    {
        shift_hours(&l.s[i], mon, &s, &e);
        CHECK(s == want[i].wd_s && e == want[i].wd_e);
        shift_hours(&l.s[i], sat, &s, &e);
        CHECK(s == want[i].we_s && e == want[i].we_e);
        shift_hours(&l.s[i], sun, &s, &e);
        CHECK(s == want[i].we_s && e == want[i].we_e);
    }
    CHECK(strcmp(l.s[3].name, "Middle breaker") == 0);

    // Full: no eighth. Round trip through the file.
    Shift x;
    CHECK(shift_parse_entry("9a-5p Office", &x) && shift_add(&l, &x) == -1);
    TextBuf b;
    textbuf_init(&b);
    CHECK(shift_serialize(&l, &b));
    CHECK(strstr(b.data, "Late shift|12:30-19:00|11:00-17:30\n") != NULL);
    CHECK(strstr(b.data, "Manager|08:00-17:00\n") != NULL);
    shift_parse(&l2, b.data);
    CHECK(l2.count == l.count && l2.s[1].we_end == 1050 && l2.s[6].we_start == SHIFT_SAME);
    textbuf_free(&b);
    shift_remove(&l, 0);
    CHECK(l.count == SHIFT_MAX - 1 && strcmp(l.s[0].name, "Late shift") == 0);

    // Typed entries and ranges.
    CHECK(shift_parse_entry("12:30p-7p Late shift", &x) && x.start == 750 && x.end == 1140 &&
          x.we_start == SHIFT_SAME && strcmp(x.name, "Late shift") == 0);
    CHECK(shift_parse_entry("9a-5p office", &x) && strcmp(x.name, "Office") == 0 &&
          x.start == 540 && x.end == 1020);
    CHECK(!shift_parse_entry("Late shift", &x));          // no hours
    CHECK(!shift_parse_entry("12:30p Late shift", &x));   // no end
    CHECK(!shift_parse_entry("9a-5p", &x));               // no name
    CHECK(shift_parse_range("11a-5:30p", &s, &e) && s == 660 && e == 1050);
    CHECK(shift_parse_range(" 10:30-14:30 ", &s, &e) && s == 630 && e == 870);
    CHECK(!shift_parse_range("11a", &s, &e) && !shift_parse_range("", &s, &e));
    CHECK(!shift_parse_range("11a-5p extra", &s, &e));
    char r[16];
    shift_range_text(750, 1140, r, sizeof(r));
    CHECK(strcmp(r, "12:30p-7p") == 0);

    static CalList c;
    char ctext[] = "2026-11-23|10:00-16:30|-|Work: Early shift\n2026-11-23|09:00|-|Workout\n";
    cal_parse(&c, ctext);
    CHECK(c.count == 2 && cal_is_work(&c.ev[0]) && !cal_is_work(&c.ev[1]));
}

static void test_cal_spans(void)
{
    static CalList c, c2;
    char text[] = "2026-11-20|13:30-17:30|-|Workshop\n"
                  "2026-11-20..2026-11-22|09:00-12:00|-|Trip\n"
                  "2026-12-24..2026-12-26|--:--|Y|Holidays\n"
                  "2026-11-27..2026-11-28|19:00|W|Weekend thing\n"
                  "2026-11-25..2026-11-20|--:--|-|Backwards\n"
                  "2026-11-25..2026-13-01|--:--|-|Bad end\n"
                  "2026-11-20|25:00-26:00|-|Bad time\n";
    cal_parse(&c, text);
    CHECK(c.count == 4);
    CHECK(c.ev[0].minute == 810 && c.ev[0].end_minute == 1050 && cal_span(&c.ev[0]) == 0);
    CHECK(cal_span(&c.ev[1]) == 2 && c.ev[1].end.d == 22);

    // Spans: every day from start to end; repeats move the whole span.
    CHECK(cal_occurs_on(&c.ev[1], D(2026, 11, 21)) && cal_occurs_on(&c.ev[1], D(2026, 11, 22)));
    CHECK(!cal_occurs_on(&c.ev[1], D(2026, 11, 23)));
    CHECK(cal_day_index(&c.ev[1], D(2026, 11, 22)) == 2);
    CHECK(cal_occurs_on(&c.ev[2], D(2027, 12, 26)) && !cal_occurs_on(&c.ev[2], D(2027, 12, 27)));
    CHECK(cal_occurs_on(&c.ev[3], D(2026, 12, 5)) && cal_day_index(&c.ev[3], D(2026, 12, 5)) == 1);
    CHECK(!cal_occurs_on(&c.ev[3], D(2026, 12, 6)));

    char s[32];
    cal_time_text(&c.ev[0], D(2026, 11, 20), s, sizeof(s));
    CHECK(strcmp(s, "1:30p-5:30p") == 0);
    cal_time_text(&c.ev[1], D(2026, 11, 20), s, sizeof(s));
    CHECK(strcmp(s, "9a-") == 0);
    cal_time_text(&c.ev[1], D(2026, 11, 21), s, sizeof(s));
    CHECK(strcmp(s, "all") == 0);
    cal_time_text(&c.ev[1], D(2026, 11, 22), s, sizeof(s));
    CHECK(strcmp(s, "-12p") == 0);
    cal_time_text(&c.ev[2], D(2026, 12, 25), s, sizeof(s));
    CHECK(strcmp(s, "all") == 0);
    cal_label(&c.ev[1], D(2026, 11, 21), s, sizeof(s));
    CHECK(strcmp(s, "Trip (2/3)") == 0);
    cal_label(&c.ev[0], D(2026, 11, 20), s, sizeof(s));
    CHECK(strcmp(s, "Workshop") == 0);

    // On the trip's 2nd day it sorts with all-day events, before 10:00.
    char t2[] = "2026-11-21|10:00|-|Brunch\n2026-11-20..2026-11-22|09:00-12:00|-|Trip\n";
    cal_parse(&c2, t2);
    int idx[4];
    CHECK(cal_events_on(&c2, D(2026, 11, 21), idx, 4) == 2 && idx[0] == 1);

    // Round trip keeps ranges and spans.
    TextBuf b;
    textbuf_init(&b);
    CHECK(cal_serialize(&c, &b));
    CHECK(strstr(b.data, "2026-11-20|13:30-17:30|-|Workshop\n") != NULL);
    CHECK(strstr(b.data, "2026-11-20..2026-11-22|09:00-12:00|-|Trip\n") != NULL);
    CHECK(strstr(b.data, "2026-12-24..2026-12-26|--:--|Y|Holidays\n") != NULL);
    cal_parse(&c2, b.data);
    CHECK(c2.count == 4 && cal_span(&c2.ev[2]) == 2 && c2.ev[3].end_minute == CAL_NO_END);
    textbuf_free(&b);
}

static bool hol_is(int y, int m, int d, const char *name)
{
    const char *names[2];
    int n = holidays_on(D(y, m, d), names, 2);
    for (int i = 0; i < n; i++)
        if (strcmp(names[i], name) == 0)
            return true;
    return false;
}

static void test_holidays(void)
{
    // 2026, checked against a printed calendar.
    CHECK(hol_is(2026, 1, 1, "New Year's Day"));
    CHECK(hol_is(2026, 1, 19, "MLK Day"));
    CHECK(hol_is(2026, 2, 16, "Presidents' Day"));
    CHECK(hol_is(2026, 3, 8, "Clocks forward 1h"));
    CHECK(hol_is(2026, 4, 5, "Easter"));
    CHECK(hol_is(2026, 5, 10, "Mother's Day"));
    CHECK(hol_is(2026, 5, 25, "Memorial Day"));
    CHECK(hol_is(2026, 6, 21, "Father's Day"));
    CHECK(hol_is(2026, 9, 7, "Labor Day"));
    CHECK(hol_is(2026, 10, 12, "Columbus Day"));
    CHECK(hol_is(2026, 11, 1, "Clocks back 1h"));
    CHECK(hol_is(2026, 11, 26, "Thanksgiving"));
    CHECK(hol_is(2026, 12, 25, "Christmas"));
    // Other years: Easter moves a lot; Memorial Day is the last Monday.
    Date e = holiday_easter(2024);
    CHECK(e.m == 3 && e.d == 31);
    e = holiday_easter(2025);
    CHECK(e.m == 4 && e.d == 20);
    e = holiday_easter(2038);
    CHECK(e.m == 4 && e.d == 25);
    CHECK(hol_is(2027, 5, 31, "Memorial Day") && hol_is(2025, 11, 27, "Thanksgiving"));
    CHECK(hol_is(2025, 3, 9, "Clocks forward 1h"));
    // An ordinary day has none; next from Nov 22 2026 is Thanksgiving.
    const char *names[2];
    CHECK(holidays_on(D(2026, 11, 23), names, 2) == 0);
    Date when;
    const char *name;
    CHECK(holiday_next(D(2026, 11, 22), 14, &when, &name) && when.d == 26 &&
          strcmp(name, "Thanksgiving") == 0);
    CHECK(!holiday_next(D(2026, 8, 1), 10, &when, &name));
    // Every year 1950..2100: each holiday exactly once, on a real date.
    int bad = 0;
    for (int y = 1950; y <= 2100; y++)
    {
        int count = 0;
        for (Date d = D(y, 1, 1); d.y == y; d = date_add_days(d, 1))
            count += holidays_on(d, names, 2);
        bad += count != 21;
    }
    CHECK(bad == 0);
}

static void test_people(void)
{
    static CalList c;
    char text[] = "1996-03-14|--:--|B|Sam\n"
                  "1990-06-02|--:--|A|Mom & Dad\n"
                  "1904-11-25|--:--|B|Kim\n"
                  "2000-02-29|--:--|B|Leap\n";
    cal_parse(&c, text);
    CHECK(c.count == 4);
    CHECK(cal_is_person(&c.ev[0]) && cal_is_person(&c.ev[1]));
    CHECK(cal_occurs_on(&c.ev[0], D(2026, 3, 14)) && !cal_occurs_on(&c.ev[0], D(2026, 3, 15)));
    CHECK(cal_years_on(&c.ev[0], D(2026, 3, 14)) == 30);
    CHECK(cal_years_on(&c.ev[0], D(2026, 3, 13)) == 29);
    CHECK(cal_years_on(&c.ev[2], D(2026, 11, 25)) == -1);   // year unknown

    char out[64];
    cal_label(&c.ev[0], D(2026, 3, 14), out, sizeof(out));
    CHECK(strcmp(out, "Sam turns 30") == 0);
    cal_label(&c.ev[0], D(2026, 3, 9), out, sizeof(out));    // shown days before
    CHECK(strcmp(out, "Sam turns 30") == 0);
    cal_label(&c.ev[1], D(2026, 6, 2), out, sizeof(out));
    CHECK(strcmp(out, "Mom & Dad: 36th anniversary") == 0);
    cal_label(&c.ev[2], D(2026, 11, 25), out, sizeof(out));
    CHECK(strcmp(out, "Kim's birthday") == 0);
    c.ev[1].start.y = 2015;
    cal_label(&c.ev[1], D(2026, 6, 2), out, sizeof(out));
    CHECK(strcmp(out, "Mom & Dad: 11th anniversary") == 0);
    c.ev[1].start.y = 2004;
    cal_label(&c.ev[1], D(2026, 6, 2), out, sizeof(out));
    CHECK(strcmp(out, "Mom & Dad: 22nd anniversary") == 0);

    // Feb 29 birthdays only show in leap years, like other yearly events.
    CHECK(cal_occurs_on(&c.ev[3], D(2028, 2, 29)) && !cal_occurs_on(&c.ev[3], D(2027, 2, 28)));

    // Repeat key swaps birthday and anniversary.
    CHECK(cal_next_repeat('B') == 'A' && cal_next_repeat('A') == 'B');

    char name[CAL_TEXT_MAX + 1];
    int year;
    CHECK(cal_parse_person("Sam 1996", name, sizeof(name), &year) && year == 1996 &&
          strcmp(name, "Sam") == 0);
    CHECK(cal_parse_person("  Mom & Dad  ", name, sizeof(name), &year) &&
          year == CAL_YEAR_UNKNOWN && strcmp(name, "Mom & Dad") == 0);
    CHECK(cal_parse_person("Route 66", name, sizeof(name), &year) &&
          year == CAL_YEAR_UNKNOWN && strcmp(name, "Route 66") == 0);
    CHECK(!cal_parse_person("1996", name, sizeof(name), &year));
    CHECK(!cal_parse_person("   ", name, sizeof(name), &year));

    // Round trip keeps B and A.
    TextBuf b;
    textbuf_init(&b);
    CHECK(cal_serialize(&c, &b));
    CHECK(strstr(b.data, "1996-03-14|--:--|B|Sam\n") != NULL);
    static CalList c2;
    cal_parse(&c2, b.data);
    CHECK(c2.count == 4 && c2.ev[1].repeat == 'A');
    textbuf_free(&b);
}

static void test_habits(void)
{
    static HabitList l, l2;
    Date fri = D(2026, 11, 20);
    char text[] = "# c\nStretch|2026-11-14|xx-xxxx\nRead|2026-11-19|x\nbad line\n|2026-01-01|x\n";
    habit_parse(&l, text);
    CHECK(l.count == 2);
    CHECK(habit_done(&l.h[0], D(2026, 11, 14)) && !habit_done(&l.h[0], D(2026, 11, 16)));
    CHECK(habit_done(&l.h[0], fri) && !habit_done(&l.h[0], D(2026, 11, 21)));
    CHECK(habit_streak(&l.h[0], fri) == 4);                 // 17..20
    CHECK(habit_streak(&l.h[1], fri) == 1);                 // today not done yet: 19
    CHECK(habit_done_count(&l, fri) == 1);

    habit_set(&l.h[1], fri, true);
    CHECK(habit_streak(&l.h[1], fri) == 2 && l.h[1].len == 2);
    habit_set(&l.h[1], D(2026, 11, 17), true);              // before the start
    CHECK(l.h[1].start.d == 17 && strcmp(l.h[1].days, "x-xx") == 0);
    habit_set(&l.h[0], D(2026, 11, 16), true);
    CHECK(habit_streak(&l.h[0], fri) == 7);

    TextBuf b;
    textbuf_init(&b);
    CHECK(habit_serialize(&l, &b));
    CHECK(strstr(b.data, "Read|2026-11-17|x-xx\n") != NULL);
    habit_parse(&l2, b.data);
    CHECK(l2.count == 2 && habit_streak(&l2.h[0], fri) == 7);
    textbuf_free(&b);

    // A habit untouched for over a year: the history window moves on.
    Habit *h = &l.h[0];
    Date later = date_add_days(fri, 500);
    habit_set(h, later, true);
    CHECK(h->len <= HABIT_DAYS && habit_done(h, later) && !habit_done(h, fri));
    for (int i = 0; i < 450; i++)
        habit_set(h, date_add_days(later, i), true);
    CHECK(h->len == HABIT_DAYS && habit_streak(h, date_add_days(later, 449)) == HABIT_DAYS);

    // Weekly goals (weeks Mon-Sun; Fri 20 Nov is in the week of Mon 16).
    static HabitList g;
    char gt[] = "Gym|2026-11-02|x-x-x--x-x----x--x|3/w\nRead|2026-11-19|x|bogus\n";
    habit_parse(&g, gt);
    CHECK(g.count == 2 && g.h[0].goal == 3 && g.h[1].goal == 0);
    // Nov 2-8: 2,4,6 done (3). Nov 9-15: 9,11 done (2). Nov 16-22: 16,19 (2).
    CHECK(habit_week_count(&g.h[0], D(2026, 11, 4)) == 3);
    CHECK(habit_week_count(&g.h[0], fri) == 2);
    CHECK(!habit_satisfied(&g.h[0], fri));
    CHECK(habit_streak(&g.h[0], fri) == 0);                 // last week missed the goal
    habit_set(&g.h[0], fri, true);                           // 3 this week now
    CHECK(habit_satisfied(&g.h[0], fri) && habit_streak(&g.h[0], fri) == 1);
    CHECK(habit_satisfied(&g.h[0], D(2026, 11, 21)));       // goal met: Sat is free
    CHECK(habit_best_streak(&g.h[0], fri) == 1);
    CHECK(habit_done_last(&g.h[0], fri, 7) == 3);           // 14..20: 16, 19, 20
    TextBuf gb;
    textbuf_init(&gb);
    CHECK(habit_serialize(&g, &gb) && strstr(gb.data, "|3/w\n") != NULL &&
          strstr(gb.data, "Read|2026-11-19|x\n") != NULL);
    textbuf_free(&gb);
    CHECK(habit_next_goal(0) == 1 && habit_next_goal(6) == 0);
    char gs[24];
    habit_goal_text(3, gs, sizeof(gs));
    CHECK(strcmp(gs, "3 times a week") == 0);
    habit_goal_text(0, gs, sizeof(gs));
    CHECK(strcmp(gs, "every day") == 0);
    // Daily best streak.
    CHECK(habit_best_streak(&l.h[0], date_add_days(later, 449)) == HABIT_DAYS);

    habit_clear(&l);
    CHECK(habit_add(&l, "  Water | 8 ", fri) == 0 && strcmp(l.h[0].name, "Water   8") == 0);
    CHECK(habit_add(&l, "   ", fri) == -1);
    habit_rename(&l.h[0], "Drink water");
    CHECK(strcmp(l.h[0].name, "Drink water") == 0);
    habit_remove(&l, 0);
    CHECK(l.count == 0);
}

static void test_bmp(void)
{
    enum { W = 256, H = 140 };
    static uint8_t pix[W * H], back[W * H];
    for (int i = 0; i < W * H; i++)
        pix[i] = (uint8_t)((i * 7 + i / W) % 5);
    uint32_t pal[5] = { 0xF4ECD0, 0x282838, 0x3060A0, 0xC03030, 0x388840 };
    size_t len;
    uint8_t *f = bmp4_encode(pix, W, H, pal, 5, &len);
    CHECK(f != NULL && len == 118 + 128 * H && f[0] == 'B' && f[1] == 'M');
    CHECK(f[54] == 0xD0 && f[55] == 0xEC && f[56] == 0xF4);   // palette is BGR
    CHECK(bmp4_decode(f, len, back, W, H) && memcmp(pix, back, sizeof(pix)) == 0);
    CHECK(!bmp4_decode(f, len, back, W, H - 1));              // wrong size
    CHECK(!bmp4_decode(f, 100, back, W, H));                  // truncated
    f[28] = 8;                                                 // 8 bpp: not ours
    CHECK(!bmp4_decode(f, len, back, W, H));
    free(f);
    // Odd width pads rows to 4 bytes.
    uint8_t small[3 * 2] = { 1, 2, 3, 4, 0, 1 }, sb[6];
    f = bmp4_encode(small, 3, 2, pal, 5, &len);
    CHECK(f != NULL && len == 118 + 4 * 2 && bmp4_decode(f, len, sb, 3, 2) &&
          memcmp(small, sb, 6) == 0);
    free(f);
}

// ---------------------------------------------------------------- alarms

static void test_alarm(void)
{
    static AlarmList l;
    char text[] = "07:30|MTWTF--|on|Wake up\n"
                  "22:00|-------|off|Never\n"
                  "7:30|MTWTFSS|on|Bad time\n"
                  "08:00|MTWTFS|on|Short days\n"
                  "09:00|mtwtfss|on|Lowercase\n";
    alarm_parse(&l, text);
    CHECK(l.count == 3);
    CHECK(l.a[0].minute == 450 && l.a[0].on && strcmp(l.a[0].label, "Wake up") == 0);
    CHECK(alarm_on_weekday(&l.a[0], 1));   // Monday
    CHECK(alarm_on_weekday(&l.a[0], 5));   // Friday
    CHECK(!alarm_on_weekday(&l.a[0], 6));  // Saturday
    CHECK(!alarm_on_weekday(&l.a[0], 0));  // Sunday
    CHECK(!l.a[1].on);
    CHECK(strcmp(l.a[2].days, "MTWTFSS") == 0);

    TextBuf b;
    textbuf_init(&b);
    CHECK(alarm_serialize(&l, &b));
    static AlarmList l2;
    alarm_parse(&l2, b.data);
    CHECK(l2.count == 3 && l2.a[1].on == false && strcmp(l2.a[0].days, "MTWTF--") == 0);
    textbuf_free(&b);
}

// ---------------------------------------------------------------- ini

static void test_ini(void)
{
    static Ini ini;
    char text[1024];
    snprintf(text, sizeof(text), "%sMy App = /apps/my.nds\r\n", INI_DEFAULT_CONFIG);
    ini_parse(&ini, text);
    CHECK(strcmp(ini_get(&ini, "launch", "boot", "x"), "/BOOT.NDS") == 0);
    CHECK(strcmp(ini_get(&ini, "APPS", "my app", "x"), "/apps/my.nds") == 0);
    CHECK(strcmp(ini_get(&ini, "apps", "missing", "def"), "def") == 0);
    CHECK(strcmp(ini_get(&ini, "ui", "sounds", "x"), "on") == 0);
    CHECK(ini.count == 4);

    // ini_text_set keeps comments and order.
    char *t = ini_text_set("# c\r\n[ui]\r\nclicks=on\r\nbrightness = 1\r\n[apps]\r\nA=/a.nds",
                           "UI", "brightness", "3");
    CHECK(t != NULL &&
          strcmp(t, "# c\n[ui]\nclicks=on\nbrightness=3\n[apps]\nA=/a.nds\n") == 0);
    free(t);
    t = ini_text_set("[ui]\nclicks=on\n[apps]\nbrightness=9\n", "ui", "brightness", "2");
    CHECK(t != NULL &&
          strcmp(t, "[ui]\nbrightness=2\nclicks=on\n[apps]\nbrightness=9\n") == 0);
    free(t);
    t = ini_text_set("[launch]\nboot=/BOOT.NDS\n", "ui", "brightness", "4");
    CHECK(t != NULL && strcmp(t, "[launch]\nboot=/BOOT.NDS\n\n[ui]\nbrightness=4\n") == 0);
    free(t);
    t = ini_text_set("", "ui", "clicks", "off");
    CHECK(t != NULL && strcmp(t, "[ui]\nclicks=off\n") == 0);
    free(t);
    t = ini_text_set("# [ui]\n# brightness=1\n", "ui", "brightness", "1");
    CHECK(t != NULL && strcmp(t, "# [ui]\n# brightness=1\n\n[ui]\nbrightness=1\n") == 0);
    free(t);
}

// ---------------------------------------------------------------- files

static void write_raw(const char *path, const char *s)
{
    FILE *f = fopen(path, "wb");
    fputs(s, f);
    fclose(f);
}

static void test_files(void)
{
    char p[PDA_PATH_MAX], bak[PDA_PATH_MAX], tmp[PDA_PATH_MAX];
    tmp_path(p, sizeof(p), "TODO.TXT");
    text_sibling(p, ".BAK", bak, sizeof(bak));
    text_sibling(p, ".TMP", tmp, sizeof(tmp));
    CHECK(strstr(bak, "/TODO.BAK") != NULL);

    char sib[64];
    text_sibling("sd:/pda.dir/README", ".BAK", sib, sizeof(sib));
    CHECK(strcmp(sib, "sd:/pda.dir/README.BAK") == 0);

    char *data;
    size_t len;
    CHECK(text_load(p, 1024, &data, &len) == LOAD_MISSING);

    CHECK(text_save(p, "one\n", 4));
    CHECK(text_load(p, 1024, &data, &len) == LOAD_OK && strcmp(data, "one\n") == 0);
    free(data);
    CHECK(access(bak, F_OK) != 0);  // First save has no previous version.

    CHECK(text_save(p, "two\n", 4));
    CHECK(text_load(p, 1024, &data, &len) == LOAD_OK && strcmp(data, "two\n") == 0);
    free(data);
    char *b = text_read_file(bak, 1024, &len);
    CHECK(b != NULL && strcmp(b, "one\n") == 0);
    free(b);
    CHECK(access(tmp, F_OK) != 0);

    // Main file deleted: backup is used.
    remove(p);
    CHECK(text_load(p, 1024, &data, &len) == LOAD_FROM_BAK && strcmp(data, "one\n") == 0);
    free(data);

    // Over the size limit.
    write_raw(p, "0123456789");
    CHECK(text_read_file(p, 9, &len) == NULL);
    data = text_read_file(p, 10, &len);
    CHECK(data != NULL && len == 10);
    free(data);

    // Large file read across buffer growth.
    char *big = malloc(20000);
    memset(big, 'x', 19999);
    big[19999] = '\0';
    CHECK(text_save(p, big, 19999));
    data = text_read_file(p, 20000, &len);
    CHECK(data != NULL && len == 19999 && memcmp(data, big, 19999) == 0);
    free(data);
    free(big);
}

static void test_notes(void)
{
    char dir[PDA_PATH_MAX];
    tmp_path(dir, sizeof(dir), "NOTES");
    mkdir(dir, 0755);

    NoteIndex *idx = malloc(sizeof(NoteIndex));
    CHECK(note_index_load(idx, dir) && idx->count == 0);
    CHECK(note_next_id(idx) == 1);

    char p[PDA_PATH_MAX];
    note_path(dir, 1, p, sizeof(p));
    write_raw(p, "Shopping\r\nmilk\r\neggs\r\n");
    note_path(dir, 3, p, sizeof(p));
    write_raw(p, "");
    tmp_path(p, sizeof(p), "NOTES/README.TXT");
    write_raw(p, "not a note");
    CHECK(note_index_load(idx, dir) && idx->count == 2);
    CHECK(idx->n[0].id == 1 && strcmp(idx->n[0].title, "Shopping") == 0);
    CHECK(strcmp(idx->n[1].title, "(untitled 3)") == 0);
    CHECK(note_next_id(idx) == 2);

    // Sketches share the numbering: S0002.BMP with its title in S0002.TXT,
    // S0004.BMP without one.
    sketch_path(dir, 2, p, sizeof(p));
    CHECK(strstr(p, "/S0002.BMP") != NULL);
    write_raw(p, "BM");
    tmp_path(p, sizeof(p), "NOTES/S0002.TXT");
    write_raw(p, "Floor plan\n");
    tmp_path(p, sizeof(p), "NOTES/S0004.BMP");
    write_raw(p, "BM");
    CHECK(note_index_load(idx, dir) && idx->count == 4);
    CHECK(idx->n[1].id == 2 && idx->n[1].sketch && strcmp(idx->n[1].title, "Floor plan") == 0);
    CHECK(!idx->n[0].sketch && !idx->n[2].sketch);
    CHECK(idx->n[3].id == 4 && idx->n[3].sketch && strcmp(idx->n[3].title, "Sketch 4") == 0);
    CHECK(note_next_id(idx) == 5);
    remove(p);
    tmp_path(p, sizeof(p), "NOTES/S0002.TXT");
    remove(p);
    sketch_path(dir, 2, p, sizeof(p));
    remove(p);
    CHECK(note_index_load(idx, dir) && idx->count == 2);

    // Checklists: L0002.TXT, in the same numbering, titled by line 1.
    checklist_path(dir, 2, p, sizeof(p));
    CHECK(strstr(p, "/L0002.TXT") != NULL);
    write_raw(p, "Groceries\n[ ] Milk\n[x] Eggs\n");
    tmp_path(p, sizeof(p), "NOTES/L0005.TXT");
    write_raw(p, "");
    CHECK(note_index_load(idx, dir) && idx->count == 4);
    CHECK(idx->n[1].id == 2 && idx->n[1].checklist && !idx->n[1].sketch);
    CHECK(strcmp(idx->n[1].title, "Groceries") == 0);
    CHECK(idx->n[1].items == 2 && idx->n[1].done == 1);   // glance numbers for the list
    CHECK(idx->n[3].items == 0 && idx->n[0].items == 0);
    CHECK(!idx->n[0].checklist && !idx->n[2].checklist);
    CHECK(idx->n[3].id == 5 && idx->n[3].checklist && strcmp(idx->n[3].title, "List 5") == 0);
    CHECK(note_next_id(idx) == 4);
    remove(p);
    checklist_path(dir, 2, p, sizeof(p));
    remove(p);
    CHECK(note_index_load(idx, dir) && idx->count == 2);

    bool done = true;
    CHECK(strcmp(checklist_item("[ ] Milk", &done), "Milk") == 0 && !done);
    CHECK(strcmp(checklist_item("[x] Eggs", &done), "Eggs") == 0 && done);
    CHECK(strcmp(checklist_item("[X]Bread", &done), "Bread") == 0 && done);
    CHECK(strcmp(checklist_item("Plain line", &done), "Plain line") == 0 && !done);
    CHECK(strcmp(checklist_item("[", &done), "[") == 0 && !done);
    CHECK(strcmp(checklist_item("", &done), "") == 0 && !done);
    char cl[32];
    checklist_line(true, "Eggs", cl, sizeof(cl));
    CHECK(strcmp(cl, "[x] Eggs") == 0);
    checklist_line(false, "", cl, sizeof(cl));
    CHECK(strcmp(cl, "[ ] ") == 0);

    // Bills: B0006.TXT in the same numbering.
    bills_path(dir, 6, p, sizeof(p));
    CHECK(strstr(p, "/B0006.TXT") != NULL);
    write_raw(p, "November\r\nRent|1200.00\r\nPhone|45.99\r\n\r\n");
    CHECK(note_index_load(idx, dir) && idx->count == 3);
    CHECK(idx->n[2].id == 6 && idx->n[2].bills && !idx->n[2].checklist && !idx->n[2].sketch);
    CHECK(strcmp(idx->n[2].title, "November") == 0);
    CHECK(idx->n[2].total == 124599 && idx->n[2].items == 0);
    remove(p);

    long c = -1;
    CHECK(money_parse("12", &c) && c == 1200);
    CHECK(money_parse("12.5", &c) && c == 1250);
    CHECK(money_parse("$1,200.00", &c) && c == 120000);
    CHECK(money_parse(" -3.10 ", &c) && c == -310);
    CHECK(money_parse("-$3", &c) && c == -300);
    CHECK(money_parse(".99", &c) && c == 99);
    CHECK(money_parse("9999999.99", &c) && c == MONEY_MAX);
    CHECK(!money_parse("", &c) && !money_parse("$", &c) && !money_parse(".", &c));
    CHECK(!money_parse("1.234", &c) && !money_parse("12a", &c) && !money_parse(",5", &c));
    CHECK(!money_parse("10000000", &c));
    char m[32];
    money_format(120000, m, sizeof(m));
    CHECK(strcmp(m, "$1,200.00") == 0);
    money_format(0, m, sizeof(m));
    CHECK(strcmp(m, "$0.00") == 0);
    money_format(-310, m, sizeof(m));
    CHECK(strcmp(m, "-$3.10") == 0);
    money_format(123456789012LL, m, sizeof(m));
    CHECK(strcmp(m, "$1,234,567,890.12") == 0);
    money_format(99999, m, sizeof(m));
    CHECK(strcmp(m, "$999.99") == 0);

    char bt[64];
    bill_item("Rent|1200.00", bt, sizeof(bt), &c);
    CHECK(strcmp(bt, "Rent") == 0 && c == 120000);
    bill_item("A|B | 5", bt, sizeof(bt), &c);
    CHECK(strcmp(bt, "A|B") == 0 && c == 500);
    bill_item("Just text", bt, sizeof(bt), &c);
    CHECK(strcmp(bt, "Just text") == 0 && c == 0);
    bill_item("Odd|abc", bt, sizeof(bt), &c);
    CHECK(strcmp(bt, "Odd|abc") == 0 && c == 0);
    bill_line("Phone", 4599, bt, sizeof(bt));
    CHECK(strcmp(bt, "Phone|45.99") == 0);
    bill_line("Refund", -250, bt, sizeof(bt));
    CHECK(strcmp(bt, "Refund|-2.50") == 0);
    CHECK(bill_split("Electric 84.50", bt, sizeof(bt), &c) && strcmp(bt, "Electric") == 0 &&
          c == 8450);
    CHECK(bill_split("Car insurance  $1,020", bt, sizeof(bt), &c) &&
          strcmp(bt, "Car insurance") == 0 && c == 102000);
    CHECK(!bill_split("Rent", bt, sizeof(bt), &c) && !bill_split("1200", bt, sizeof(bt), &c));
    CHECK(!bill_split("Gas bill", bt, sizeof(bt), &c));

    NoteDoc bd;
    note_doc_init(&bd);
    CHECK(note_doc_from_text(&bd, "Nov\nRent|1200.00\nPhone|45.99\nRefund|-2.50\nnote\n"));
    CHECK(bills_total(&bd) == 120000 + 4599 - 250);
    note_doc_free(&bd);

    NoteDoc d;
    note_doc_init(&d);
    CHECK(note_doc_from_text(&d, "Title\r\nline 2\n\nline 4"));
    CHECK(d.count == 4 && strcmp(d.lines[2], "") == 0 && strcmp(d.lines[3], "line 4") == 0);
    CHECK(note_doc_insert(&d, 1, "inserted"));
    CHECK(note_doc_set(&d, 0, "New title"));
    note_doc_delete(&d, 3);
    TextBuf b;
    textbuf_init(&b);
    CHECK(note_doc_to_text(&d, &b));
    CHECK(strcmp(b.data, "New title\ninserted\nline 2\nline 4\n") == 0);
    textbuf_free(&b);

    // 16 KB cap: fill with 100-char lines.
    char line[101];
    memset(line, 'n', 100);
    line[100] = '\0';
    int added = 0;
    while (note_doc_insert(&d, d.count, line))
        added++;
    CHECK(note_doc_size(&d) <= NOTE_MAX_BYTES);
    CHECK(note_doc_size(&d) + 101 > NOTE_MAX_BYTES);
    CHECK(added > 150);

    textbuf_init(&b);
    CHECK(note_doc_to_text(&d, &b));
    note_path(dir, 2, p, sizeof(p));
    CHECK(text_save(p, b.data, b.len));
    textbuf_free(&b);
    char *back;
    size_t len;
    CHECK(text_load(p, NOTE_MAX_BYTES, &back, &len) == LOAD_OK);
    NoteDoc d2;
    note_doc_init(&d2);
    CHECK(note_doc_from_text(&d2, back));
    CHECK(d2.count == d.count);
    free(back);
    note_doc_free(&d2);
    note_doc_free(&d);

    // Empty doc serializes to an empty string.
    textbuf_init(&b);
    CHECK(note_doc_to_text(&d, &b) && b.len == 0 && b.data != NULL);
    textbuf_free(&b);
    free(idx);
}

// ---------------------------------------------------------------- input

static unsigned rng = 12345;
static int jitter(int amp)
{
    rng = rng * 1103515245u + 12345u;
    return (int)((rng >> 16) % (unsigned)(2 * amp + 1)) - amp;
}

// A swipe through the word's keys: 8 points per key-to-key segment, jitter
// of +-amp px, and a slight overshoot at the ends like a real stylus.
static int swipe_path_for(const char *w, int amp, SwipePt *out)
{
    int n = 0, px = -1, py = -1;
    for (const char *c = w; *c; c++)
    {
        int x, y;
        layout_letter_center(*c, &x, &y);
        if (px >= 0 && (x != px || y != py))
        {
            for (int s = 1; s <= 8; s++)
                out[n++] = (SwipePt){ (short)(px + (x - px) * s / 8 + jitter(amp)),
                                      (short)(py + (y - py) * s / 8 + jitter(amp)) };
        }
        else if (px < 0)
        {
            out[n++] = (SwipePt){ (short)(x + jitter(amp)), (short)(y + jitter(amp)) };
        }
        px = x;
        py = y;
    }
    return n;
}

static void test_layout(void)
{
    KbdKey keys[KBD_MAX_KEYS];
    int n = layout_keys(PAGE_LETTERS, keys, KBD_MAX_KEYS);
    CHECK(n == 10 + 9 + 9 + 6);
    for (int i = 0; i < n; i++)
    {
        CHECK(keys[i].x >= 0 && keys[i].x + keys[i].w <= 256);
        CHECK(keys[i].y >= KBD_ROWS_Y && keys[i].y + keys[i].h <= 192);
        CHECK(keys[i].w >= 25);   // the point of the big keyboard
    }
    int x, y;
    CHECK(layout_letter_center('q', &x, &y) && x == 12 && y == KBD_ROWS_Y + KBD_ROW_H / 2);
    CHECK(layout_hit(keys, n, x, y) == 0);
    CHECK(!layout_letter_center('Q', &x, &y));
    n = layout_keys(PAGE_SYMBOLS, keys, KBD_MAX_KEYS);
    CHECK(n == 10 + 10 + 9 + 6);
}

static void test_swipe(void)
{
    size_t len;
    char *list = text_read_file("../../data/words.bin", 1 << 20, &len);
    CHECK(list != NULL);
    if (list == NULL)
        return;
    CHECK(swipe_load(list, len));
    CHECK(swipe_word_count() > 30000);

    static const char *WORDS[] = {
        "the", "and", "you", "that", "with", "have", "this", "from", "they", "would",
        "there", "their", "about", "people", "time", "hello", "thanks", "water", "phone",
        "school", "money", "dinner", "morning", "tomorrow", "birthday", "meeting", "call",
        "buy", "milk", "rent", "work", "home", "friday", "doctor", "remember", "great",
        "good", "night", "week", "pick",
    };
    const int nw = (int)(sizeof(WORDS) / sizeof(WORDS[0]));
    int top1 = 0, top3 = 0;
    for (int i = 0; i < nw; i++)
    {
        SwipePt p[SWIPE_PATH_MAX];
        int n = swipe_path_for(WORDS[i], 4, p);
        char out[3][SWIPE_WORD_MAX + 1];
        int got = swipe_decode(p, n, out, 3);
        bool in3 = false;
        for (int k = 0; k < got; k++)
            in3 |= strcasecmp(out[k], WORDS[i]) == 0;
        bool first = got > 0 && strcasecmp(out[0], WORDS[i]) == 0;
        top1 += first;
        top3 += in3;
        if (!first)
            fprintf(stderr, "swipe miss: %s -> %s %s %s\n", WORDS[i], got > 0 ? out[0] : "-",
                    got > 1 ? out[1] : "", got > 2 ? out[2] : "");
    }
    printf("swipe: %d/%d first choice, %d/%d in top 3\n", top1, nw, top3, nw);
    CHECK(top1 * 100 >= nw * 80);
    CHECK(top3 * 100 >= nw * 95);

    // A short stroke and garbage input.
    SwipePt one[1] = { { 10, 50 } };
    char out[3][SWIPE_WORD_MAX + 1];
    CHECK(swipe_decode(one, 1, out, 3) == 0);
    free(list);
}

// Redraws a template's shape with jitter, a new size and position, and the
// strokes in reverse order and direction.
static int ink_from_shape(const char *shape, int size, int ox, int oy, int amp, InkPt *out)
{
    InkPt tmp[64];
    int n = 0, stroke = 0;
    const char *s = shape;
    while (*s)
    {
        if (*s == '/') { stroke++; s++; continue; }
        if (*s < '0' || *s > '9') { s++; continue; }
        int x = (int)strtol(s, (char **)&s, 10);
        s++;
        int y = (int)strtol(s, (char **)&s, 10);
        tmp[n++] = (InkPt){ (short)x, (short)y, (unsigned char)stroke };
    }
    // Densify (a stylus reports many points) and reverse.
    InkPt dense[HWR_MAX_POINTS];
    int m = 0;
    for (int i = n - 1; i >= 0; i--)
    {
        bool first_of_stroke = i == n - 1 || tmp[i + 1].stroke != tmp[i].stroke;
        if (!first_of_stroke)
        {
            const InkPt *a = &tmp[i + 1], *b = &tmp[i];
            for (int k = 1; k <= 6; k++)
                dense[m++] = (InkPt){ (short)(a->x + (b->x - a->x) * k / 6),
                                      (short)(a->y + (b->y - a->y) * k / 6),
                                      (unsigned char)(stroke - b->stroke) };
        }
        else
        {
            dense[m++] = (InkPt){ tmp[i].x, tmp[i].y, (unsigned char)(stroke - tmp[i].stroke) };
        }
    }
    for (int i = 0; i < m; i++)
    {
        out[i] = dense[i];
        out[i].x = (short)(ox + dense[i].x * size / 100 + jitter(amp));
        out[i].y = (short)(oy + dense[i].y * size / 100 + jitter(amp));
    }
    return m;
}

static void test_hwr(void)
{
    hwr_init();
    static const struct { char ch; HwrSet set; const char *shape; } CASES[] = {
        { 'a', HWR_LETTERS, "0,100 50,0 100,100 / 25,55 75,55" },
        { 'a', HWR_LETTERS, "80,50 60,40 35,42 20,60 22,85 40,100 60,95 80,75 / 80,40 80,100" },
        { 'b', HWR_LETTERS, "15,0 15,100 / 15,65 35,45 60,42 80,55 85,75 75,95 50,100 25,95 15,85" },
        { 'c', HWR_LETTERS, "90,15 70,2 40,0 15,15 2,50 15,85 40,100 70,98 90,85" },
        { 'd', HWR_LETTERS, "85,0 85,100 / 85,60 65,45 40,42 20,55 15,75 25,95 50,100 70,95 85,85" },
        { 'e', HWR_LETTERS, "90,0 10,0 10,100 90,100 / 10,50 70,50" },
        { 'e', HWR_LETTERS, "15,70 85,70 80,52 60,40 35,42 18,58 15,78 28,95 50,100 70,98 85,88" },
        { 'h', HWR_LETTERS, "10,0 10,100 / 90,0 90,100 / 10,50 90,50" },
        { 'k', HWR_LETTERS, "10,0 10,100 / 85,0 10,60 / 35,45 90,100" },
        { 'l', HWR_LETTERS, "10,0 10,100 90,100" },
        { 'm', HWR_LETTERS, "5,100 10,0 50,60 90,0 95,100" },
        { 'n', HWR_LETTERS, "15,40 15,100 / 15,60 35,44 60,42 80,55 82,75 82,100" },
        { 'o', HWR_LETTERS, "50,0 15,15 0,50 15,85 50,100 85,85 100,50 85,15 50,0" },
        { 'p', HWR_LETTERS, "10,100 10,0 / 10,0 65,0 90,15 90,40 65,55 10,55" },
        { 'r', HWR_LETTERS, "20,40 20,100 / 20,65 35,48 55,40 75,42" },
        { 's', HWR_LETTERS, "85,12 60,0 35,0 12,15 15,38 50,50 85,62 90,85 65,100 35,100 10,88" },
        { 't', HWR_LETTERS, "0,0 100,0 / 50,0 50,100" },
        { 'u', HWR_LETTERS, "10,0 10,70 25,95 50,100 75,95 90,70 90,0" },
        { 'v', HWR_LETTERS, "0,0 50,100 100,0" },
        { 'w', HWR_LETTERS, "0,0 25,100 50,35 75,100 100,0" },
        { 'x', HWR_LETTERS, "10,0 90,100 / 90,0 10,100" },
        { 'y', HWR_LETTERS, "10,0 50,50 / 90,0 50,50 50,100" },
        { 'z', HWR_LETTERS, "10,0 90,0 10,100 90,100" },
        { '2', HWR_DIGITS, "15,20 30,5 55,0 80,10 85,30 75,50 10,100 90,100" },
        { '3', HWR_DIGITS, "15,10 40,0 70,2 85,18 80,38 55,50 / 55,50 85,62 90,82 70,100 40,100 15,90" },
        { '4', HWR_DIGITS, "70,100 70,0 10,70 95,70" },
        { '5', HWR_DIGITS, "85,0 20,0 15,45 45,38 75,45 88,65 80,90 55,100 30,100 10,90" },
        { '7', HWR_DIGITS, "10,0 90,0 40,100" },
        { '8', HWR_DIGITS, "50,50 20,35 20,12 50,0 80,12 80,35 50,50 15,68 15,90 50,100 85,90 85,68 50,50" },
        { '0', HWR_DIGITS, "50,0 25,10 15,50 25,90 50,100 75,90 85,50 75,10 50,0" },
    };
    const int nc = (int)(sizeof(CASES) / sizeof(CASES[0]));
    int top1 = 0, top3 = 0, total = 0;
    for (int i = 0; i < nc; i++)
    {
        for (int trial = 0; trial < 3; trial++)
        {
            InkPt ink[HWR_MAX_POINTS];
            int size = 50 + trial * 25;
            int n = ink_from_shape(CASES[i].shape, size, 20 + trial * 30, 45, 2, ink);
            char out[3];
            int got = hwr_recognize(ink, n, CASES[i].set, out, 3);
            total++;
            bool in3 = memchr(out, CASES[i].ch, (size_t)got) != NULL;
            top1 += got > 0 && out[0] == CASES[i].ch;
            top3 += in3;
            if (got == 0 || out[0] != CASES[i].ch)
                fprintf(stderr, "hwr: '%c' (size %d) -> %.*s\n", CASES[i].ch, size, got, out);
        }
    }
    printf("hwr: %d/%d first choice, %d/%d in top 3\n", top1, total, top3, total);
    CHECK(top1 * 100 >= total * 90);
    CHECK(top3 == total);

    // A dot is a period; long flat strokes are space and backspace.
    InkPt dot[2] = { { 100, 100, 0 }, { 101, 101, 0 } };
    char out[3];
    CHECK(hwr_recognize(dot, 2, HWR_LETTERS, out, 3) == 1 && out[0] == '.');
    InkPt right[2] = { { 20, 100, 0 }, { 200, 110, 0 } };
    InkPt left[2] = { { 200, 100, 0 }, { 20, 104, 0 } };
    InkPt tall[2] = { { 20, 40, 0 }, { 200, 150, 0 } };
    CHECK(hwr_gesture(right, 2, 256) == ' ');
    CHECK(hwr_gesture(left, 2, 256) == '\b');
    CHECK(hwr_gesture(tall, 2, 256) == 0);
}

// Reads shape as the user would write it (size, place, wobble) and returns
// the first guess, or 0.
static char hwr_read(const char *shape, HwrSet set, int size, int ox, int amp)
{
    InkPt ink[HWR_MAX_POINTS];
    int n = ink_from_shape(shape, size, ox, 45, amp, ink);
    char out[3];
    return hwr_recognize(ink, n, set, out, 3) > 0 ? out[0] : 0;
}

static void test_hwr_user(void)
{
    hwr_user_forget(0);
    CHECK(hwr_set_of('a') == HWR_LETTERS && hwr_set_of('?') == HWR_LETTERS);
    CHECK(hwr_set_of('7') == HWR_DIGITS && hwr_set_of(':') == HWR_DIGITS);
    CHECK(hwr_set_of('.') < 0 && hwr_set_of('A') < 0 && hwr_set_of(' ') < 0);

    // A way of writing 'q' no built-in shape looks like: a square spiral.
    const char *spiral = "50,50 60,50 60,60 40,60 40,40 70,40 70,70 30,70 30,30 80,30";
    char before = hwr_read(spiral, HWR_LETTERS, 70, 60, 2);
    InkPt ink[HWR_MAX_POINTS];
    for (int i = 0; i < 3; i++)
    {
        int n = ink_from_shape(spiral, 60 + i * 15, 30 + i * 20, 45, 2, ink);
        CHECK(hwr_user_add('q', ink, n));
    }
    CHECK(hwr_user_count('q') == 3 && hwr_user_total() == 3 && hwr_user_dirty());
    char after = hwr_read(spiral, HWR_LETTERS, 80, 90, 3);   // another size and place
    CHECK(after == 'q');
    if (before == 'q')
        fprintf(stderr, "hwr user: the spiral already read as q\n");

    // Dots and characters outside the sets are not samples.
    InkPt dot[2] = { { 100, 100, 0 }, { 101, 101, 0 } };
    CHECK(!hwr_user_add('a', dot, 2) && !hwr_user_add('.', ink, 10) && !hwr_user_add('A', ink, 10));

    // At most HWR_USER_PER_CHAR of a character: the oldest go.
    int n = ink_from_shape(spiral, 70, 60, 45, 2, ink);
    for (int i = 0; i < 10; i++)
        hwr_user_add('q', ink, n);
    CHECK(hwr_user_count('q') == HWR_USER_PER_CHAR);

    // With many samples loaded (more than the full-match limit), the
    // built-in shapes still read right.
    static const struct { char ch; const char *shape; } OWN[] = {
        { 'a', "80,50 60,40 35,42 20,60 22,85 40,100 60,95 80,75 / 80,40 80,100" },
        { 'b', "15,0 15,100 / 15,65 35,45 60,42 80,55 85,75 75,95 50,100 25,95 15,85" },
        { 'e', "15,70 85,70 80,52 60,40 35,42 18,58 15,78 28,95 50,100 70,98 85,88" },
        { 'h', "10,0 10,100 / 90,0 90,100 / 10,50 90,50" },
        { 'n', "15,40 15,100 / 15,60 35,44 60,42 80,55 82,75 82,100" },
        { 'o', "50,0 15,15 0,50 15,85 50,100 85,85 100,50 85,15 50,0" },
        { 's', "85,12 60,0 35,0 12,15 15,38 50,50 85,62 90,85 65,100 35,100 10,88" },
        { 't', "0,0 100,0 / 50,0 50,100" },
    };
    const int no = (int)(sizeof(OWN) / sizeof(OWN[0]));
    for (int i = 0; i < no; i++)
        for (int k = 0; k < HWR_USER_PER_CHAR; k++)
        {
            n = ink_from_shape(OWN[i].shape, 50 + k * 10, 20 + k * 15, 45, 3, ink);
            hwr_user_add(OWN[i].ch, ink, n);
        }
    CHECK(hwr_user_total() == HWR_USER_PER_CHAR * (no + 1));
    int right = 0, total = 0;
    static const struct { char ch; const char *shape; } CHECKS[] = {
        { 'c', "90,15 70,2 40,0 15,15 2,50 15,85 40,100 70,98 90,85" },
        { 'k', "10,0 10,100 / 85,0 10,60 / 35,45 90,100" },
        { 'l', "10,0 10,100 90,100" },
        { 'v', "0,0 50,100 100,0" },
        { 'w', "0,0 25,100 50,35 75,100 100,0" },
        { 'x', "10,0 90,100 / 90,0 10,100" },
        { 'z', "10,0 90,0 10,100 90,100" },
        { 'a', "80,50 60,40 35,42 20,60 22,85 40,100 60,95 80,75 / 80,40 80,100" },
        { 'o', "50,0 15,15 0,50 15,85 50,100 85,85 100,50 85,15 50,0" },
        { 't', "0,0 100,0 / 50,0 50,100" },
    };
    for (int i = 0; i < (int)(sizeof(CHECKS) / sizeof(CHECKS[0])); i++)
        for (int trial = 0; trial < 3; trial++)
        {
            total++;
            char got = hwr_read(CHECKS[i].shape, HWR_LETTERS, 55 + trial * 20, 25 + trial * 25, 2);
            right += got == CHECKS[i].ch;
            if (got != CHECKS[i].ch)
                fprintf(stderr, "hwr user: '%c' -> '%c'\n", CHECKS[i].ch, got ? got : '?');
        }
    printf("hwr with %d samples: %d/%d first choice\n", hwr_user_total(), right, total);
    CHECK(right * 100 >= total * 90);
    CHECK(hwr_read(spiral, HWR_LETTERS, 80, 90, 3) == 'q');

    // The file round trip keeps every sample and the same answers.
    TextBuf b;
    textbuf_init(&b);
    CHECK(hwr_user_serialize(&b) && !hwr_user_dirty());
    int saved = hwr_user_total();
    char *copy = strdup(b.data);
    hwr_user_forget(0);
    CHECK(hwr_user_total() == 0);
    hwr_user_parse(copy);
    CHECK(hwr_user_total() == saved && hwr_user_count('q') == HWR_USER_PER_CHAR && !hwr_user_dirty());
    CHECK(hwr_read(spiral, HWR_LETTERS, 80, 90, 3) == 'q');
    free(copy);
    textbuf_free(&b);
    // Lines that do not fit are skipped: short, bad character, comments.
    char bad[] = "# x\nq|1,2 3,4\nA|0,0\n.|1,1\n\n";
    hwr_user_parse(bad);
    CHECK(hwr_user_total() == 0);

    // Forgetting one character leaves the rest.
    for (int k = 0; k < 2; k++)
    {
        n = ink_from_shape(spiral, 70, 60, 45, 2, ink);
        hwr_user_add('q', ink, n);
        n = ink_from_shape(OWN[0].shape, 70, 60, 45, 2, ink);
        hwr_user_add('a', ink, n);
    }
    hwr_user_forget('q');
    CHECK(hwr_user_count('q') == 0 && hwr_user_count('a') == 2);
    hwr_user_forget(0);
}

int main(void)
{
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/pda_host_test_%d", (int)getpid());
    mkdir(tmpdir, 0755);

    test_date();
    test_date_relative();
    test_todo();
    test_todo_repeat();
    test_cal();
    test_cal_spans();
    test_work();
    test_people();
    test_holidays();
    test_habits();
    test_bmp();
    test_alarm();
    test_ini();
    test_files();
    test_notes();
    test_layout();
    test_swipe();
    test_hwr();
    test_hwr_user();

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: could not remove %s\n", tmpdir);

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
