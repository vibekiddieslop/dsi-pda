// Work shifts.
//
// The picker: one big key per saved shift with that day's hours (weekend
// hours on Sat and Sun), then "Custom time slot" (hours picked each time)
// and, while there is room, "New shift". Back cancels; the menu edits the
// saved shifts.
//
// SCR_SHIFTS: the saved shifts as a list, to add, change and delete them.
// A shift is typed as its hours and name in one go: "12:30p-7p Late shift".
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "app.h"
#include "device.h"
#include "screens.h"
#include "sfx.h"
#include "store/work.h"
#include "ui/editor.h"
#include "ui/widgets.h"

static void save_shifts(void)
{
    if (!app_save_shifts())
        ui_toast("%s", app_last_error());
    ui_dirty();
}

// ---- adding a shift type, from the picker or the list

static void (*after_add)(int index);

static void new_shift_done(bool ok, const char *text)
{
    if (!ok || text[0] == '\0')
        return;
    Shift s;
    if (!shift_parse_entry(text, &s))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Like: 9a-5p Office");
        return;
    }
    int i = shift_add(&g_shifts, &s);
    if (i < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Max 7 shifts.");
        return;
    }
    sfx_play(SFX_SAVED);
    ui_toast("Saved. Set weekend hours in the list.");
    save_shifts();
    if (after_add != NULL)
        after_add(i);
}

static void new_shift(void (*then)(int index))
{
    if (g_shifts.count >= SHIFT_MAX)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Max 7 shifts.");
        return;
    }
    after_add = then;
    editor_open("NEW SHIFT", "Like: 9a-5p Office", "", 40, new_shift_done);
}

// ---------------------------------------------------------------- picker

enum { KEY_SHIFT, KEY_CUSTOM, KEY_NEW };

static Date day;
static ShiftDone on_done;
static int sel = 0;

// The picker's keys: the saved shifts, the custom slot, then New shift.
static int key_count(void)
{
    return g_shifts.count + 1 + (g_shifts.count < SHIFT_MAX ? 1 : 0);
}

static int key_kind(int k)
{
    return k < g_shifts.count ? KEY_SHIFT : k == g_shifts.count ? KEY_CUSTOM : KEY_NEW;
}

static void key_rect(int i, int *x, int *y, int *w, int *h)
{
    *x = 4 + (i % 2) * 126;
    *w = 122;
    *y = 27 + (i / 2) * 41;
    *h = 38;
}

static void picked_new(int index)
{
    sel = index;   // the new shift's key, ready to tap
}

static void pick(int k)
{
    device_click();
    int kind = key_kind(k);
    if (kind == KEY_NEW)
    {
        new_shift(picked_new);
        return;
    }
    ShiftDone cb = on_done;
    ui_pop();
    if (cb != NULL)
        cb(kind == KEY_SHIFT ? k : SHIFT_CUSTOM);
}

static void picker_update(const Input *in)
{
    int n = key_count();
    if (sel >= n)
        sel = n - 1;
    if (in->touch_down)
    {
        for (int i = 0; i < n; i++)
        {
            int x, y, w, h;
            key_rect(i, &x, &y, &w, &h);
            if (in->tx >= x && in->tx < x + w && in->ty >= y && in->ty < y + h)
            {
                pick(i);
                return;
            }
        }
        return;
    }
    int old = sel;
    if ((in->repeat & KEY_LEFT) && sel % 2 == 1)
        sel--;
    if ((in->repeat & KEY_RIGHT) && sel % 2 == 0 && sel + 1 < n)
        sel++;
    if ((in->repeat & KEY_UP) && sel >= 2)
        sel -= 2;
    if ((in->repeat & KEY_DOWN) && sel + 2 < n)
        sel += 2;
    if (sel != old)
        ui_dirty();
    if (in->down & KEY_A)
        pick(sel);
}

static void hours_text(int k, char *out, int size)
{
    int kind = key_kind(k);
    if (kind == KEY_CUSTOM)
    {
        snprintf(out, (size_t)size, "pick times");
        return;
    }
    if (kind == KEY_NEW)
    {
        snprintf(out, (size_t)size, "save one");
        return;
    }
    int s, e;
    shift_hours(&g_shifts.s[k], day, &s, &e);
    shift_range_text(s, e, out, size);
}

static const char *key_name(int k)
{
    int kind = key_kind(k);
    return kind == KEY_SHIFT ? g_shifts.s[k].name : kind == KEY_CUSTOM ? "Custom slot"
                                                                       : "+ New shift";
}

static void picker_draw_top(Surface *s)
{
    gfx_header(s, "Work shift", NULL);
    gfx_paper(s, PAPER_Y);
    char buf[40];
    int wd = date_weekday(day);
    snprintf(buf, sizeof(buf), "%s %d %s", DATE_DAY[wd], day.d, DATE_MONTH3[day.m - 1]);
    gfx_text(s, 20, PAPER_TEXT(0), buf, C_ACCENT);
    gfx_text(s, 20, PAPER_TEXT(1), wd == 0 || wd == 6 ? "Weekend hours" : "Weekday hours",
             C_INK_SOFT);
    // The highlighted key in full.
    int k = sel < key_count() ? sel : 0;
    char hours[24];
    hours_text(k, hours, sizeof(hours));
    gfx_text(s, 20, PAPER_TEXT(3), key_name(k), C_BROWN);
    gfx_text(s, 20, PAPER_TEXT(4), hours, C_INK);
    if (g_shifts.count == 0)
    {
        gfx_text(s, 20, PAPER_TEXT(7), "New shift saves a shift.", C_INK_SOFT);
        gfx_text(s, 20, PAPER_TEXT(8), "Its hours fill in", C_INK_SOFT);
        gfx_text(s, 20, PAPER_TEXT(9), "by themselves.", C_INK_SOFT);
    }
    gfx_text(s, 20, PAPER_TEXT(11), "One shift per day.", C_INK_SOFT);
    gfx_text(s, 20, PAPER_TEXT(12), "New replaces old.", C_INK_SOFT);
    static const char *const KEYS[] = { "A", "Pick", "Y", "Edit shifts", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

static void picker_draw_bottom(Surface *s)
{
    gfx_page(s, UI_HEAD_H);
    for (int i = 0; i < key_count(); i++)
    {
        int x, y, w, h;
        key_rect(i, &x, &y, &w, &h);
        char hours[24], label[48];
        hours_text(i, hours, sizeof(hours));
        snprintf(label, sizeof(label), "%.14s\n%s", key_name(i), hours);   // 14 fit a key
        int kind = key_kind(i);
        gfx_button(s, x, y, w, h, label, ICON_NONE,
                   kind == KEY_SHIFT ? SEC_HABITS : kind == KEY_CUSTOM ? SEC_NOTES : SEC_PLAIN,
                   false, true);
        if (i == sel && ui_pad_mode())
            gfx_frame(s, x - 1, y - 1, w + 2, h + 2, C_ACCENT);
    }
}

static void picker_more(void)
{
    ui_push(&SCR_SHIFTS);
}

static const Screen shift_screen = {
    .update = picker_update,
    .draw_top = picker_draw_top,
    .draw_bottom = picker_draw_bottom,
    .title = "Work shift",
    .icon = ICON_WORK,
    .sec = SEC_CAL,
    .on_more = picker_more,
    .more_icon = ICON_EDIT,
};

void shift_open(Date d, ShiftDone done)
{
    day = d;
    on_done = done;
    sel = 0;
    ui_push(&shift_screen);
}

// ---------------------------------------------------------------- the list

#define BOX_W 26

static List list;
static Bar bar;

static bool any(void)
{
    return g_shifts.count > 0;
}

static void list_draw_top(Surface *s)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%d of %d", g_shifts.count, SHIFT_MAX);
    gfx_header(s, "Shifts", buf);
    gfx_paper(s, PAPER_Y);
    if (!any())
    {
        gfx_icon(s, 112, 42, ICON_WORK, 2);
        gfx_text_center(s, 136, PAPER_TEXT(6), "No shifts.", C_INK);
        gfx_text_center(s, 136, PAPER_TEXT(7), "Tap Add. Type hours and", C_INK_SOFT);
        gfx_text_center(s, 136, PAPER_TEXT(8), "a name: 9a-5p Office", C_INK_SOFT);
        return;
    }
    list.count = g_shifts.count;
    list_fix(&list);
    const Shift *sh = &g_shifts.s[list.sel];
    int row = title_draw(s, sh->name, 0, C_BROWN) + 1;
    int x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "WEEKDAYS", SEC_MID(SEC_CAL), C_SNOW);
    shift_range_text(sh->start, sh->end, buf, sizeof(buf));
    gfx_text(s, x + 5, PAPER_TEXT(row), buf, C_INK);
    row++;
    x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "SAT+SUN", SEC_MID(SEC_HABITS), C_INK);
    if (sh->we_start == SHIFT_SAME)
        gfx_text(s, x + 5, PAPER_TEXT(row), "same hours", C_INK_SOFT);
    else
    {
        shift_range_text(sh->we_start, sh->we_end, buf, sizeof(buf));
        gfx_text(s, x + 5, PAPER_TEXT(row), buf, C_INK);
    }
    gfx_text(s, 20, PAPER_TEXT(10), "Planned days keep", C_INK_SOFT);
    gfx_text(s, 20, PAPER_TEXT(11), "their old hours.", C_INK_SOFT);
    static const char *const KEYS[] = { "A", "Edit", "X", "Add", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

static void list_row(Surface *s, int i, int y, bool selected)
{
    (void)selected;
    const Shift *sh = &g_shifts.s[i];
    gfx_icon(s, 5, y + 1, ICON_WORK, 1);
    char hours[24];
    shift_range_text(sh->start, sh->end, hours, sizeof(hours));
    if (sh->we_start != SHIFT_SAME)
        strcat(hours, "*");   // other hours on the weekend
    int hx = SCREEN_W - 8 - (int)strlen(hours) * FONT_W;
    gfx_text(s, hx, y + 6, hours, C_ACCENT);
    gfx_text_n(s, BOX_W + 6, y + 6, sh->name, (hx - 8 - BOX_W - 6) / FONT_W, C_INK);
}

static void list_draw_bottom(Surface *s)
{
    list.count = g_shifts.count;
    list_draw(&list, s, list_row);
    if (!any())
    {
        gfx_text_center(s, SCREEN_W / 2, 70, "No shifts.", C_INK_SOFT);
        gfx_text_center(s, SCREEN_W / 2, 86, "Tap Add.", C_INK_SOFT);
    }
    else if (g_shifts.count < 6)
    {
        gfx_text(s, 10, UI_BAR_Y - 14, "* other weekend hours", C_INK_SOFT);
    }
    bar.n = 3;
    bar.item[0] = (BarItem){ "Edit", ICON_EDIT, SEC_PLAIN, any() };
    bar.item[1] = (BarItem){ "Sat+Sun", ICON_NONE, SEC_PLAIN, any() };
    bar.item[2] = (BarItem){ "Add", ICON_PLUS, SEC_CAL, g_shifts.count < SHIFT_MAX };
    bar_draw(s, &bar);
}

static void edit_done(bool ok, const char *text)
{
    if (!ok || list.sel >= g_shifts.count)
        return;
    Shift s;
    if (!shift_parse_entry(text, &s))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Like: 9a-5p Office");
        return;
    }
    Shift *old = &g_shifts.s[list.sel];
    s.we_start = old->we_start;   // the weekend hours stay
    s.we_end = old->we_end;
    *old = s;
    sfx_play(SFX_SAVED);
    save_shifts();
}

static void do_edit(void)
{
    if (!any())
        return;
    const Shift *sh = &g_shifts.s[list.sel];
    char init[48], hours[24];
    shift_range_text(sh->start, sh->end, hours, sizeof(hours));
    snprintf(init, sizeof(init), "%s %s", hours, sh->name);
    editor_open("EDIT SHIFT", "Weekday hours, name.", init, 40, edit_done);
}

static void weekend_done(bool ok, const char *text)
{
    if (!ok || list.sel >= g_shifts.count)
        return;
    Shift *sh = &g_shifts.s[list.sel];
    char t[48];
    snprintf(t, sizeof(t), "%s", text);
    if (text_trim(t)[0] == '\0')
    {
        sh->we_start = sh->we_end = SHIFT_SAME;
    }
    else if (!shift_parse_range(t, &sh->we_start, &sh->we_end))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Like 11a-5:30p. Empty: same");
        return;
    }
    sfx_play(SFX_SAVED);
    save_shifts();
}

static void do_weekend(void)
{
    if (!any())
        return;
    const Shift *sh = &g_shifts.s[list.sel];
    char init[24] = "";
    if (sh->we_start != SHIFT_SAME)
        shift_range_text(sh->we_start, sh->we_end, init, sizeof(init));
    editor_open_number("SAT+SUN HOURS", "Like 11a-5:30p. Empty: same", init, 20, weekend_done);
}

static void added(int index)
{
    list.count = g_shifts.count;
    list_show(&list, index);
}

static void do_add(void)
{
    new_shift(added);
}

static ShiftList undo_shifts;   // the list before a delete, for Undo

static void shift_undo(void)
{
    g_shifts = undo_shifts;
    list.count = g_shifts.count;
    list_fix(&list);
    save_shifts();
}

static void delete_yes(void)
{
    if (list.sel >= g_shifts.count)
        return;
    undo_shifts = g_shifts;
    shift_remove(&g_shifts, list.sel);
    sfx_play(SFX_DELETE);
    list.count = g_shifts.count;
    list_fix(&list);
    save_shifts();
    ui_undo_offer(shift_undo);
}

static void do_delete(void)
{
    if (any())
        ui_confirm("Delete shift?", g_shifts.s[list.sel].name, "Delete", delete_yes);
}

static void list_update_fn(const Input *in)
{
    list.count = g_shifts.count;
    int b = bar_update(&bar, in);
    if (b == 0)
        do_edit();
    else if (b == 1)
        do_weekend();
    else if (b == 2)
        do_add();
    if (b >= 0)
        return;
    ListEvent ev = list_update(&list, in);
    if (ev.kind == LE_OPEN || ev.kind == LE_BOX)
        do_edit();
}

static void list_enter(void)
{
    if (list.height == 0)
        list_init(&list, UI_HEAD_H, UI_BAR_Y - UI_HEAD_H, 0);
    list.count = g_shifts.count;
    list_fix(&list);
}

const Screen SCR_SHIFTS = {
    .enter = list_enter,
    .update = list_update_fn,
    .draw_top = list_draw_top,
    .draw_bottom = list_draw_bottom,
    .title = "Work shifts",
    .icon = ICON_WORK,
    .sec = SEC_CAL,
    .on_add = do_add,
    .on_delete = do_delete,
    .can_delete = any,
};
