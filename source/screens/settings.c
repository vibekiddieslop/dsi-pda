// Settings: the owner's name, sounds, brightness, colors, how to type,
// handwriting training, work shifts, holidays and the controls. Tap a row to pick
// it; the bar changes it. The top screen explains the picked row.
#include <stdio.h>

#include <nds.h>

#include "app.h"
#include "device.h"
#include "screens.h"
#include "sfx.h"
#include "input/hwr.h"
#include "ui/editor.h"
#include "ui/widgets.h"

enum { S_NAME, S_SOUNDS, S_BRIGHT, S_THEME, S_TYPING, S_TRAIN, S_SHIFTS, S_HOLIDAYS,
       S_BUTTONS, S_COUNT };
#define BOX_W 26

static List list;
static Bar bar;

static int brightness(void)
{
    int b = device_brightness();
    return b ? b : 3;   // not known until it is set once: assume the middle
}

// ---------------------------------------------------------------- top

static void lines(Surface *s, int row, const char *const *text, uint8_t ink)
{
    for (int i = 0; text[i] != NULL; i++)
        gfx_text(s, 20, PAPER_TEXT(row + i), text[i], ink);
}

static void key_line(Surface *s, int row, const char *key, const char *what)
{
    int x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, key, C_INK, C_WHITE);
    gfx_text(s, x + 5, PAPER_TEXT(row), what, C_INK);
}

static void draw_top(Surface *s)
{
    char buf[64];
    gfx_header(s, "Settings", NULL);
    gfx_paper(s, PAPER_Y);
    switch (list.sel)
    {
        case S_NAME:
        {
            const char *name = device_user_name();
            title_draw(s, name[0] ? name : "(no name)", 0, C_INK);
            static const char *const T[] = { "Home uses this name.",
                                             "Empty: uses the DSi name.",
                                             "", NULL };
            lines(s, 3, T, C_INK_SOFT);
            break;
        }
        case S_SOUNDS:
        {
            title_draw(s, sfx_enabled() ? "Sounds on" : "Sounds off", 0, C_INK);
            static const char *const T[] = { "Beeps and jingles.", "Alarms always ring.",
                                             "", NULL };
            lines(s, 3, T, C_INK_SOFT);
            break;
        }
        case S_BRIGHT:
        {
            snprintf(buf, sizeof(buf), "Level %d of 5", brightness());
            title_draw(s, buf, 0, C_INK);
            for (int i = 0; i < 5; i++)
                gfx_panel(s, 20 + i * 44, PAPER_Y + 3 * PAPER_ROW, 40, 20, 3,
                          i < brightness() ? SEC_MID(SEC_NOTES) : C_PAPER_SH, C_INK_SOFT);
            static const char *const T[] = { "Shortcut: hold L+R,", "then press Up or Down.",
                                             NULL };
            lines(s, 6, T, C_INK_SOFT);
            break;
        }
        case S_THEME:
        {
            title_draw(s, gfx_theme_name(gfx_theme()), 0, C_INK);
            static const char *const T[] = { "Colors:",
                                             "",
                                             "Classic: cream and brown.",
                                             "",
                                             "Dark: for night.",
                                             "",
                                             "Candy: blue and pink.",
                                             "", NULL };
            lines(s, 3, T, C_INK_SOFT);
            break;
        }
        case S_TYPING:
        {
            title_draw(s, editor_uses_pen() ? "Handwriting" : "Keyboard", 0, C_INK);
            static const char *const T[] = { "Keyboard: tap keys,",
                                             "or swipe a word.",
                                             "",
                                             "",
                                             "Handwriting: write one",
                                             "letter at a time.",
                                             "",
                                             "A key switches them.", NULL };
            lines(s, 3, T, C_INK_SOFT);
            break;
        }
        case S_SHIFTS:
        {
            title_draw(s, "Work shifts", 0, C_INK);
            static const char *const T[] = { "Your usual shifts.",
                                             "Calendar > Add >",
                                             "Work shift uses them.",
                                             "",
                                             "", NULL };
            lines(s, 3, T, C_INK_SOFT);
            break;
        }
        case S_TRAIN:
        {
            title_draw(s, "Handwriting", 0, C_INK);
            snprintf(buf, sizeof(buf), "%d sample%s of yours", hwr_user_total(),
                     hwr_user_total() == 1 ? "" : "s");
            gfx_text(s, 20, PAPER_TEXT(2), buf, hwr_user_total() ? C_OK : C_INK_SOFT);
            static const char *const T[] = { "Teach the pad your",
                                             "handwriting. It reads",
                                             "you better after.",
                                             "",
                                             "It also learns as you type:",
                                             "fix a wrong letter",
                                             "on the strip.", NULL };
            lines(s, 4, T, C_INK_SOFT);
            break;
        }
        case S_HOLIDAYS:
        {
            title_draw(s, device_holidays() ? "Holidays on" : "Holidays off", 0, C_INK);
            static const char *const T[] = { "US holidays on the calendar,",
                                             "day list and Home.",
                                             "Also the 2 clock changes.",
                                             "(The DSi clock does not",
                                             "change itself.)", NULL };
            lines(s, 3, T, C_INK_SOFT);
            break;
        }
        case S_BUTTONS:
            gfx_text(s, 20, PAPER_TEXT(0), "Same on every page:", C_INK_SOFT);
            key_line(s, 1, "D-pad", "move");
            key_line(s, 2, "A", "check or open");
            key_line(s, 3, "B", "back");
            key_line(s, 4, "X", "add");
            key_line(s, 5, "Y", "menu");
            key_line(s, 6, "L R", "page, month, ticker");
            key_line(s, 7, "START", "done. On Home: games");
            key_line(s, 8, "SELECT", "Undo; Home: ticker");
            gfx_text(s, 20, PAPER_TEXT(9), "While typing:", C_INK_SOFT);
            key_line(s, 10, "START", "save");
            key_line(s, 11, "SELECT", "cancel");
            key_line(s, 12, "B", "delete letter");
            break;
    }
}

// ---------------------------------------------------------------- bottom

static void draw_switch(Surface *s, int y, bool on)
{
    gfx_panel(s, 3, y + 4, 22, 11, 4, on ? SEC_MID(SEC_TODO) : C_SHADOW, C_INK_SOFT);
    gfx_panel(s, on ? 14 : 3, y + 3, 11, 13, 4, C_WHITE, C_INK_SOFT);
}

static void draw_row(Surface *s, int i, int y, bool selected)
{
    (void)selected;
    char buf[48];
    const char *label = "";
    switch (i)
    {
        case S_NAME:
            gfx_icon(s, 5, y + 1, ICON_EDIT, 1);
            snprintf(buf, sizeof(buf), "Name: %.16s", device_user_name());
            label = buf;
            break;
        case S_SOUNDS:
            draw_switch(s, y, sfx_enabled());
            label = sfx_enabled() ? "Sounds: on" : "Sounds: off";
            break;
        case S_BRIGHT:
            gfx_icon(s, 5, y + 1, ICON_SUN, 1);
            snprintf(buf, sizeof(buf), "Brightness: %d of 5", brightness());
            label = buf;
            break;
        case S_THEME:
            gfx_icon(s, 5, y + 1, ICON_SKETCH, 1);
            snprintf(buf, sizeof(buf), "Colors: %s", gfx_theme_name(gfx_theme()));
            label = buf;
            break;
        case S_TYPING:
            gfx_icon(s, 5, y + 1, ICON_NOTES, 1);
            label = editor_uses_pen() ? "Typing: handwriting" : "Typing: keyboard";
            break;
        case S_SHIFTS:
            gfx_icon(s, 5, y + 1, ICON_WORK, 1);
            snprintf(buf, sizeof(buf), "Work shifts: %d saved", g_shifts.count);
            label = buf;
            break;
        case S_TRAIN:
            gfx_icon(s, 5, y + 1, ICON_EDIT, 1);
            label = "Train handwriting";
            break;
        case S_HOLIDAYS:
            draw_switch(s, y, device_holidays());
            label = device_holidays() ? "Holidays: on" : "Holidays: off";
            break;
        case S_BUTTONS:
            gfx_icon(s, 5, y + 1, ICON_GAMES, 1);
            label = "Controls";
            break;
    }
    gfx_text_n(s, BOX_W + 6, y + 6, label, 27, C_INK);
}

static void draw_bottom(Surface *s)
{
    list.count = S_COUNT;
    list_draw(&list, s, draw_row);
    bar.n = 0;
    switch (list.sel)
    {
        case S_NAME:
            bar.n = 1;
            bar.item[0] = (BarItem){ "Edit name", ICON_EDIT, SEC_PLAIN, true };
            break;
        case S_SOUNDS:
            bar.n = 1;
            bar.item[0] = (BarItem){ sfx_enabled() ? "Turn off" : "Turn on", ICON_SOUND,
                                     SEC_PLAIN, true };
            break;
        case S_BRIGHT:
            bar.n = 2;
            bar.item[0] = (BarItem){ "Dimmer", ICON_NONE, SEC_PLAIN, brightness() > 1 };
            bar.item[1] = (BarItem){ "Brighter", ICON_NONE, SEC_PLAIN, brightness() < 5 };
            break;
        case S_THEME:
            // One key per theme; the one in use is greyed out.
            bar.n = THEME_COUNT;
            for (int t = 0; t < THEME_COUNT; t++)
                bar.item[t] = (BarItem){ gfx_theme_name(t), ICON_NONE, SEC_PLAIN,
                                         t != gfx_theme() };
            break;
        case S_TYPING:
            bar.n = 1;
            bar.item[0] = (BarItem){ editor_uses_pen() ? "Use keyboard" : "Use handwriting",
                                     ICON_NONE, SEC_PLAIN, true };
            break;
        case S_SHIFTS:
            bar.n = 1;
            bar.item[0] = (BarItem){ "Edit shifts", ICON_WORK, SEC_PLAIN, true };
            break;
        case S_TRAIN:
            bar.n = 1;
            bar.item[0] = (BarItem){ "Start training", ICON_EDIT, SEC_SETTINGS, true };
            break;
        case S_HOLIDAYS:
            bar.n = 1;
            bar.item[0] = (BarItem){ device_holidays() ? "Turn off" : "Turn on", ICON_CAL,
                                     SEC_PLAIN, true };
            break;
    }
    bar_draw(s, &bar);
}

// ---------------------------------------------------------------- actions

static void name_done(bool ok, const char *text)
{
    if (!ok)
        return;
    app_set_config("ui", "name", text);
    sfx_play(SFX_SAVED);
    ui_dirty();
}

// The row's main change: A, a second tap, or the first bar key.
static void change(int row, int dir)
{
    switch (row)
    {
        case S_NAME:
            editor_open("YOUR NAME", "Your name?",
                        ini_get(&g_cfg, "ui", "name", ""), 10, name_done);
            return;
        case S_SOUNDS:
            device_set_sounds(!sfx_enabled());
            sfx_play(SFX_SAVED);
            break;
        case S_BRIGHT:
        {
            int b = brightness() + dir;
            if (dir == 0)
                b = brightness() % 5 + 1;   // A steps up and wraps around
            if (b >= 1 && b <= 5)
                device_set_brightness(b);
            break;
        }
        case S_THEME:
            // A key on the bar picks one (dir = theme + 1); A cycles.
            device_set_theme(dir > 0 ? dir - 1 : (gfx_theme() + 1) % THEME_COUNT);
            sfx_play(SFX_SAVED);
            break;
        case S_TYPING:
            editor_set_pen(!editor_uses_pen());
            sfx_play(SFX_SAVED);
            break;
        case S_SHIFTS:
            ui_push(&SCR_SHIFTS);
            return;
        case S_TRAIN:
            ui_push(&SCR_HWR_TRAIN);
            return;
        case S_HOLIDAYS:
            device_set_holidays(!device_holidays());
            sfx_play(SFX_SAVED);
            break;
    }
    ui_dirty();
}

static void update(const Input *in)
{
    list.count = S_COUNT;
    int b = bar_update(&bar, in);
    if (b >= 0)
    {
        int dir = 0;
        if (list.sel == S_BRIGHT)
            dir = b == 0 ? -1 : +1;
        else if (list.sel == S_THEME)
            dir = b + 1;
        change(list.sel, dir);
        return;
    }
    ListEvent ev = list_update(&list, in);
    if (ev.kind == LE_BOX || ev.kind == LE_OPEN)
        change(ev.index, 0);
    // Left and Right also set the brightness.
    if (list.sel == S_BRIGHT && (in->repeat & (KEY_LEFT | KEY_RIGHT)))
        change(S_BRIGHT, (in->repeat & KEY_LEFT) ? -1 : +1);
}

static void enter(void)
{
    if (list.height == 0)
        list_init(&list, UI_HEAD_H, UI_BAR_Y - UI_HEAD_H, BOX_W);
    list.count = S_COUNT;
    list_fix(&list);
}

const Screen SCR_SETTINGS = {
    .enter = enter,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title = "Settings",
    .icon = ICON_GEAR,
    .sec = SEC_SETTINGS,
};
