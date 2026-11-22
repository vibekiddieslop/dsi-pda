// Big-key keyboard: 25x38 px keys, letters and symbols pages, swipe typing.
//
// A tap types the key under the stylus when it lifts, so sliding onto the
// right key before lifting fixes a near miss. A stroke that starts on a
// letter and travels over other keys is a swipe: on lift it becomes the
// best matching word, and the strip above the keys offers the next two.
// Backspace fires on touch and repeats while held.
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "device.h"
#include "input/layout.h"
#include "input/swipe.h"
#include "textin.h"
#include "words_bin.h"

#define SWIPE_START_PX 24   // travel before a touch counts as a swipe
#define SUG_MAX 3

static KbdKey keys[KBD_MAX_KEYS];
static int nkeys;
static int page = PAGE_LETTERS;
static int shift = 0;               // 0 off, 1 next letter, 2 caps lock
static int shift_tap_frame = -100, frame = 0;

static bool tracking = false, swiping = false;
static SwipePt path[SWIPE_PATH_MAX];
static int path_n = 0, path_len = 0;
static int start_key = -1, hover_key = -1;
static int last_x, last_y;

static char sug[SUG_MAX][SWIPE_WORD_MAX + 1];
static int nsug = 0, sug_sel = 0;
static bool sug_space = false;      // the word was inserted with a leading space
static bool sug_cap = false;
static int sug_len = 0;             // chars the shown suggestion really inserted

static void set_page(int p)
{
    page = p;
    nkeys = layout_keys(page, keys, KBD_MAX_KEYS);
}

bool textin_sentence_start(const TextSink *t)
{
    char a = t->before(0);
    if (a == 0)
        return true;
    if (a != ' ')
        return false;
    char b = t->before(1);
    return b == '.' || b == '!' || b == '?';
}

static void auto_shift(const TextSink *t)
{
    if (shift != 2)
        shift = textin_sentence_start(t) ? 1 : 0;
}

void kbd_init(void)
{
    set_page(PAGE_LETTERS);
}

void kbd_reset(const TextSink *t)
{
    // The word index (about 300 KB) is built the first time a keyboard
    // opens, not at boot, so power-on reaches the home screen sooner.
    if (swipe_word_count() == 0)
        swipe_load((const char *)words_bin, words_bin_size);
    set_page(textin_numbers() ? PAGE_SYMBOLS : PAGE_LETTERS);
    shift = 0;
    tracking = swiping = false;
    nsug = 0;
    auto_shift(t);
}

static char shifted(char c)
{
    return (shift && c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static void type_char(const TextSink *t, char c)
{
    char s[2] = { shifted(c), 0 };
    t->insert(s);
    if (shift == 1)
        shift = 0;
    nsug = 0;
    auto_shift(t);
}

// Inserts suggestion i, replacing the one shown before when replace is set.
static void put_word(const TextSink *t, int i, bool replace)
{
    // Only what really went in is removed: near the length limit a word
    // may have been cut short.
    if (replace)
        t->backspace(sug_len);
    char w[SWIPE_WORD_MAX + 2];
    snprintf(w, sizeof(w), "%s%s", sug_space ? " " : "", sug[i]);
    if (sug_cap)
    {
        char *first = w + (sug_space ? 1 : 0);
        if (*first >= 'a' && *first <= 'z')
            *first = (char)(*first - 32);
    }
    sug_len = t->insert(w);
    sug_sel = i;
}

void kbd_text_edited(const TextSink *t)
{
    nsug = 0;
    auto_shift(t);
}

static void finish_swipe(const TextSink *t)
{
    nsug = swipe_decode(path, path_n, sug, SUG_MAX);
    if (nsug == 0)
    {
        ui_toast("No word found.");
        return;
    }
    char b = t->before(0);
    sug_space = b != 0 && b != ' ';
    sug_cap = shift != 0;
    put_word(t, 0, false);
    if (shift == 1)
        shift = 0;
}

static void press_key(const TextSink *t, int k)
{
    device_click();
    const KbdKey *key = &keys[k];
    switch (key->kind)
    {
        case KK_CHAR:
            type_char(t, key->ch);
            break;
        case KK_SPACE:
            t->insert(" ");
            nsug = 0;
            auto_shift(t);
            break;
        case KK_SHIFT:
            if (shift == 2)
                shift = 0;
            else if (frame - shift_tap_frame < 20)
                shift = 2;   // double tap: caps lock
            else
                shift = !shift;
            shift_tap_frame = frame;
            break;
        case KK_PAGE:
            set_page(page == PAGE_LETTERS ? PAGE_SYMBOLS : PAGE_LETTERS);
            nsug = 0;
            break;
        case KK_PEN:
            t->switch_mode();
            break;
        case KK_ENTER:
            t->enter();
            break;
        case KK_BKSP:
            break;   // handled on touch
    }
}

static void backspace(const TextSink *t)
{
    device_click();
    t->backspace(1);
    nsug = 0;
    auto_shift(t);
}

static void add_point(int x, int y)
{
    if (path_n > 0)
    {
        int dx = x - path[path_n - 1].x, dy = y - path[path_n - 1].y;
        if (dx * dx + dy * dy < 4)
            return;
        path_len += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    }
    if (path_n < SWIPE_PATH_MAX)
        path[path_n++] = (SwipePt){ (short)x, (short)y };
}

void kbd_update(const Input *in, const TextSink *t)
{
    frame++;
    if (in->touch_down)
    {
        // Suggestion strip.
        if (in->ty >= KBD_STRIP_Y && in->ty < KBD_ROWS_Y)
        {
            int i = in->tx * SUG_MAX / SCREEN_W;
            if (i < nsug && i != sug_sel)
            {
                device_click();
                put_word(t, i, true);
            }
            return;
        }
        if (in->ty < KBD_ROWS_Y)
            return;
        tracking = true;
        swiping = false;
        path_n = path_len = 0;
        add_point(in->tx, in->ty);
        last_x = in->tx;
        last_y = in->ty;
        start_key = hover_key = layout_hit(keys, nkeys, in->tx, in->ty);
        if (start_key >= 0 && keys[start_key].kind == KK_BKSP)
            backspace(t);
        ui_dirty_bottom();
        return;
    }
    if (!tracking)
        return;

    if (in->touching)
    {
        last_x = in->tx;
        last_y = in->ty;
        int before = path_n;
        add_point(in->tx, in->ty);
        int h = layout_hit(keys, nkeys, in->tx, in->ty);
        if (h != hover_key)
        {
            hover_key = h;
            ui_dirty_bottom();
        }
        bool on_letter = start_key >= 0 && keys[start_key].kind == KK_CHAR &&
                         keys[start_key].ch >= 'a' && keys[start_key].ch <= 'z';
        if (!swiping && page == PAGE_LETTERS && on_letter && path_len > SWIPE_START_PX &&
            hover_key != start_key)
            swiping = true;
        if (swiping && path_n != before)
            ui_dirty_bottom();   // the trail grew
        if (start_key >= 0 && keys[start_key].kind == KK_BKSP && hover_key == start_key &&
            (in->repeat & KEY_TOUCH) && !(in->down & KEY_TOUCH))
            backspace(t);
        return;
    }

    // Lifted.
    tracking = false;
    if (swiping)
    {
        swiping = false;
        finish_swipe(t);
    }
    else
    {
        int k = layout_hit(keys, nkeys, last_x, last_y);
        if (k >= 0 && keys[k].kind != KK_BKSP)
            press_key(t, k);
    }
    hover_key = -1;
    ui_dirty_bottom();   // Text changes redraw the top through the editor.
}

static const char *special_label(const KbdKey *k)
{
    switch (k->kind)
    {
        case KK_SHIFT: return shift == 2 ? "CAPS" : "Shft";
        case KK_BKSP: return "Del";
        case KK_PAGE: return page == PAGE_LETTERS ? "123" : "abc";
        case KK_PEN: return "Pen";
        case KK_SPACE: return "space";
        case KK_ENTER: return "Enter";
        default: return NULL;
    }
}

void kbd_draw(Surface *s)
{
    gfx_fill(s, 0, KBD_STRIP_Y, SCREEN_W, SCREEN_H - KBD_STRIP_Y, C_BODY);

    // Suggestion strip.
    gfx_fill(s, 0, KBD_STRIP_Y, SCREEN_W, KBD_STRIP_H, C_BODY_HI);
    if (nsug > 0)
    {
        for (int i = 0; i < nsug; i++)
        {
            int x = i * SCREEN_W / SUG_MAX, w = SCREEN_W / SUG_MAX;
            if (i == sug_sel)
                gfx_fill(s, x + 1, KBD_STRIP_Y + 1, w - 2, KBD_STRIP_H - 2, C_SEL);
            int chars = (w - 4) / FONT_W, len = (int)strlen(sug[i]);
            int shown = len < chars ? len : chars;
            gfx_text_n(s, x + w / 2 - shown * FONT_W / 2, KBD_STRIP_Y + 6, sug[i], chars,
                       i == sug_sel ? C_INK : C_LABEL);
            if (i > 0)
                gfx_vline(s, x, KBD_STRIP_Y + 3, KBD_STRIP_H - 6, C_DIM);
        }
    }
    else if (page == PAGE_LETTERS)
    {
        gfx_text_center(s, SCREEN_W / 2, KBD_STRIP_Y + 6, "Tap or swipe.",
                        C_DIM);
    }

    for (int i = 0; i < nkeys; i++)
    {
        const KbdKey *k = &keys[i];
        bool down = tracking && !swiping && i == hover_key;
        const char *label = special_label(k);
        gfx_key(s, k->x, k->y, k->w, k->h, label, down, true);
        if (k->kind == KK_SHIFT && shift)
            gfx_fill(s, k->x + 6, k->y + k->h - 8, k->w - 12, 3, C_ACCENT);
        if (k->kind == KK_CHAR)
        {
            char c[2] = { shifted(k->ch), 0 };
            int off = down ? 1 : 0;
            gfx_text2x(s, k->x + k->w / 2 - 8 + off, k->y + k->h / 2 - 8 + off, c, C_INK);
        }
    }

    if (swiping)
    {
        for (int i = 1; i < path_n; i++)
            gfx_line(s, path[i - 1].x, path[i - 1].y, path[i].x, path[i].y, C_ACCENT, 3);
    }
}
