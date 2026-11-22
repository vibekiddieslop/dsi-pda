#include "editor.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <nds.h>

#include "app.h"
#include "device.h"
#include "textin.h"
#include "ui.h"

#define WRAP 29          // Characters per line in the top-screen text box.
#define BOX_X 20
#define BOX_Y 46

static char buf[EDITOR_MAX + 1];
static int len, cursor, max_len;
static char title[24], prompt[64];
static EditorDone on_done;
static int blink;
static bool use_pen = false;     // handwriting pad instead of the keyboard

void editor_init(void)
{
    kbd_init();
    pad_init();
    handwriting_load();
    use_pen = strcasecmp(ini_get(&g_cfg, "ui", "input", "keys"), "pen") == 0;
}

bool editor_uses_pen(void)
{
    return use_pen;
}

void editor_set_pen(bool pen)
{
    use_pen = pen;
    app_set_config("ui", "input", use_pen ? "pen" : "keys");
}

static void finish(bool ok)
{
    EditorDone cb = on_done;
    char result[EDITOR_MAX + 1];
    memcpy(result, buf, sizeof(result));
    ui_pop();
    if (cb != NULL)
        cb(ok, result);
    ui_dirty();
}

static void insert_char(char c)
{
    if (len >= max_len)
        return;
    memmove(buf + cursor + 1, buf + cursor, (size_t)(len - cursor + 1));
    buf[cursor++] = c;
    len++;
}

static void backspace(void)
{
    if (cursor == 0)
        return;
    memmove(buf + cursor - 1, buf + cursor, (size_t)(len - cursor + 1));
    cursor--;
    len--;
}

// ---------------------------------------------------------------- sink

static int sink_insert(const char *s)
{
    int before = len;
    for (; *s; s++)
        insert_char(*s);
    blink = 0;
    ui_dirty();
    return len - before;
}

static void sink_backspace(int n)
{
    while (n-- > 0)
        backspace();
    blink = 0;
    ui_dirty();
}

static void sink_enter(void)
{
    finish(true);
}

static char sink_before(int back)
{
    int i = cursor - 1 - back;
    return i >= 0 ? buf[i] : 0;
}

static const TextSink sink;

static void sink_switch(void)
{
    editor_set_pen(!use_pen);
    if (use_pen)
        pad_reset(&sink);
    else
        kbd_reset(&sink);
    ui_dirty();
}

static const TextSink sink = {
    sink_insert, sink_backspace, sink_enter, sink_before, sink_switch,
};

// ---------------------------------------------------------------- screen

static const Screen editor_screen;

static void update(const Input *in)
{
    if (use_pen)
        pad_update(in, &sink);
    else
        kbd_update(in, &sink);
    if (ui_current() != &editor_screen)
        return;   // Enter closed the editor.

    if (in->down & KEY_START)
        { finish(true); return; }
    if (in->down & KEY_SELECT)
        { finish(false); return; }
    // Buttons edit the text behind the keyboard's or pad's back: they must
    // drop their suggestions (which would replace the wrong characters) and
    // recheck the automatic capital.
    bool edited = false;
    if (in->repeat & KEY_B)
    {
        backspace();
        edited = true;
    }
    if ((in->repeat & KEY_LEFT) && cursor > 0)
    {
        cursor--;
        edited = true;
    }
    if ((in->repeat & KEY_RIGHT) && cursor < len)
    {
        cursor++;
        edited = true;
    }
    if (in->down & KEY_UP)
        cursor = 0, edited = true;
    if (in->down & KEY_DOWN)
        cursor = len, edited = true;
    if (edited)
    {
        if (use_pen)
            pad_text_edited(&sink);
        else
            kbd_text_edited(&sink);
        blink = 0;
        ui_dirty();
    }

    // Cursor blink: shown for 20 frames, hidden for 10. Only the two
    // changes redraw the top screen (the text box is there).
    if (++blink >= 30)
        blink = 0;
    if (blink == 0 || blink == 20)
        ui_dirty_top();
}

static void draw_top(Surface *s)
{
    char count[12];
    snprintf(count, sizeof(count), "%d/%d", len, max_len);
    gfx_header(s, title, count);
    gfx_paper(s, PAPER_Y);
    gfx_text_n(s, BOX_X, PAPER_TEXT(0), prompt, WRAP, C_INK_SOFT);

    int rows = (max_len + WRAP - 1) / WRAP;
    if (rows > 9)
        rows = 9;
    gfx_fill(s, BOX_X - 4, BOX_Y - 4, WRAP * FONT_W + 8, rows * 12 + 6, C_WHITE);
    gfx_frame(s, BOX_X - 4, BOX_Y - 4, WRAP * FONT_W + 8, rows * 12 + 6, C_INK_SOFT);

    // Scroll so the cursor row is visible.
    int cur_row = cursor / WRAP;
    int first = cur_row >= rows ? cur_row - rows + 1 : 0;
    for (int r = 0; r < rows; r++)
    {
        int start = (first + r) * WRAP;
        if (start > len)
            break;
        char line[WRAP + 1];
        int n = len - start < WRAP ? len - start : WRAP;
        memcpy(line, buf + start, (size_t)n);
        line[n] = '\0';
        gfx_text(s, BOX_X, BOX_Y + r * 12, line, C_INK);
    }
    if (blink < 20)
    {
        int cx = BOX_X + (cursor % WRAP) * FONT_W;
        int cy = BOX_Y + (cur_row - first) * 12;
        gfx_fill(s, cx - 1, cy - 1, 2, 10, C_ACCENT);
    }

    // Help on the first two ruled rows fully below the box (its bottom edge
    // is at BOX_Y + 2 + rows * 12).
    int k = (BOX_Y + 2 + rows * 12 - PAPER_Y + PAPER_ROW) / PAPER_ROW;
    if (k + 1 < PAPER_ROWS)
    {
        gfx_text(s, BOX_X, PAPER_TEXT(k), "START: save  SELECT: cancel", C_INK_SOFT);
        gfx_text(s, BOX_X, PAPER_TEXT(k + 1), "B: delete    < >: move", C_INK_SOFT);
    }
}

static void draw_bottom(Surface *s)
{
    gfx_fill(s, 0, 0, SCREEN_W, 20, C_BODY);
    if (use_pen)
        pad_draw(s);
    else
        kbd_draw(s);
}

static const char *hint(void)
{
    return use_pen ? "Write below. START: save" : "Tap or swipe. START: save";
}

// Typing gets the full CPU speed: handwriting and swipes are real work.
static void enter(void)
{
    device_cpu_fast(true);
}

// Every way out (Enter, START, SELECT, <Back) keeps the corrections picked
// while writing.
static void leave(void)
{
    handwriting_save();
    device_cpu_fast(false);
}

static const Screen editor_screen = {
    .enter = enter,
    .leave = leave,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .hint = hint,
    .keep_b = true,
    .back_touch = true,
};

// Amounts open on the digits, and stay there if the keyboard and the pad
// are swapped mid-edit.
static bool numbers = false, numbers_next = false;

bool textin_numbers(void)
{
    return numbers;
}

void editor_open_number(const char *t, const char *p, const char *initial, int maxl,
                        EditorDone done)
{
    numbers_next = true;
    editor_open(t, p, initial, maxl, done);
}

void editor_open(const char *t, const char *p, const char *initial, int maxl, EditorDone done)
{
    numbers = numbers_next;
    numbers_next = false;
    snprintf(title, sizeof(title), "%s", t);
    snprintf(prompt, sizeof(prompt), "%s", p);
    max_len = maxl > EDITOR_MAX ? EDITOR_MAX : maxl;
    snprintf(buf, (size_t)max_len + 1, "%s", initial ? initial : "");
    len = cursor = (int)strlen(buf);
    on_done = done;
    blink = 0;
    ui_push(&editor_screen);
    if (use_pen)
        pad_reset(&sink);
    else
        kbd_reset(&sink);
}
