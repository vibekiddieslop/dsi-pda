#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "device.h"
#include "sfx.h"

#define PIXELS (SCREEN_W * SCREEN_H)

static const Screen *stack[UI_STACK_MAX];
static int depth = 0;
static bool pushing = false;      // enter() runs for a page just opened
// Each screen is redrawn (and copied to VRAM) only when it changed.
static bool dirty_top = true, dirty_bot = true;
static time_t now_cache, last_drawn_second;
static const Overlay *overlay = NULL;
static bool overlay_was_active = false;
static bool pad_mode = false;
static bool swallow = false;      // ignore the stylus until it lifts

// ---- sticky note (messages and the volume/brightness meter): top screen
#define NOTE_FRAMES 180
#define NOTE_H 34
#define NOTE_RISE 42              // px above the bottom edge when fully up
static char note_line[2][30];
static int note_frames = 0, note_total = 0;
static bool note_meter = false;
static char meter_label[12];
static int meter_value, meter_max = 1;
static int note_shown = 0;        // rise last copied to the screen
static bool note_changed = false;

// ---- page turn: the old page slides out as the new one slides in
#define TURN_FRAMES 8
static uint8_t old_top[PIXELS] __attribute__((aligned(32)));
static uint8_t old_bot[PIXELS] __attribute__((aligned(32)));
static uint8_t comp[PIXELS] __attribute__((aligned(32)));
static Surface comp_surface = { comp, NULL, false };
static int turn_left = 0, turn_dir = 1;
static bool snapped = false;      // old pages already captured this frame

// ---- undo after a delete: a key above the action bar
#define UNDO_FRAMES 300
#define UNDO_X (SCREEN_W - 92)
#define UNDO_Y (UI_BAR_Y - 27)
#define UNDO_W 86
#define UNDO_H 23
static void (*undo_fn)(void) = NULL;
static int undo_frames = 0;
static bool undo_changed = false;

static void undo_cancel(void)
{
    if (undo_fn != NULL)
        undo_changed = true;
    undo_fn = NULL;
}

void ui_undo_offer(void (*undo)(void))
{
    undo_fn = undo;
    undo_frames = UNDO_FRAMES;
    undo_changed = true;
}

// ---- header (new-style screens)
enum { HEAD_NONE, HEAD_BACK, HEAD_ADD, HEAD_MORE, HEAD_DEL };
static int head_pressed = HEAD_NONE, head_frames = 0;

// ---- pop-ups on the bottom screen
typedef enum { M_NONE, M_SHEET, M_CONFIRM } ModalKind;
#define SHEET_MAX 6
static ModalKind modal = M_NONE;
static char m_title[2][30], m_detail[30], m_yes_label[12];
static SheetItem m_items[SHEET_MAX];
static int m_n = 0, m_sel = 0, m_held = -1, m_frames = 0;
static bool m_hot = false, modal_changed = false;
static void (*m_done)(int);
static void (*m_yes)(void);

time_t ui_now(void)
{
    return now_cache;
}

bool ui_pad_mode(void)
{
    return pad_mode;
}

int ui_depth(void)
{
    return depth;
}

void ui_dirty(void)
{
    dirty_top = dirty_bot = true;
}

void ui_dirty_top(void)
{
    dirty_top = true;
}

void ui_dirty_bottom(void)
{
    dirty_bot = true;
}

// Splits text over two lines of at most `width` characters, at a space
// when there is one.
static void wrap2(const char *text, int width, char out[2][30])
{
    int len = (int)strlen(text);
    out[0][0] = out[1][0] = '\0';
    if (len <= width)
    {
        snprintf(out[0], 30, "%s", text);
        return;
    }
    int cut = width;
    for (int i = width; i > width / 2; i--)
        if (text[i] == ' ')
        {
            cut = i;
            break;
        }
    snprintf(out[0], 30, "%.*s", cut, text);
    const char *rest = text + cut;
    while (*rest == ' ')
        rest++;
    if ((int)strlen(rest) > width)
        snprintf(out[1], 30, "%.*s~", width - 1, rest);
    else
        snprintf(out[1], 30, "%s", rest);
}

void ui_toast(const char *fmt, ...)
{
    char text[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    wrap2(text, 28, note_line);
    // A note already up stays up and just changes its text.
    bool up = note_frames > 6;
    note_total = NOTE_FRAMES;
    note_frames = up ? NOTE_FRAMES - 6 : NOTE_FRAMES;
    note_meter = false;
    note_changed = true;
}

void ui_meter(const char *label, int value, int max)
{
    snprintf(meter_label, sizeof(meter_label), "%s", label);
    meter_value = value < 0 ? 0 : value > max ? max : value;
    meter_max = max > 0 ? max : 1;
    bool up = note_frames > 6;
    note_total = 90;
    note_frames = up ? 90 - 6 : 90;
    note_meter = true;
    note_changed = true;
}

// How far the note stands above the bottom edge this frame.
static int note_rise(void)
{
    static const int EASE[6] = { 12, 24, 32, 38, 41, NOTE_RISE };
    if (note_frames <= 0)
        return 0;
    int elapsed = note_total - note_frames;
    if (elapsed < 6)
        return EASE[elapsed];
    if (note_frames <= 5)
        return EASE[note_frames - 1];
    return NOTE_RISE;
}

static void draw_note(Surface *s, int rise)
{
    int x = 8, w = SCREEN_W - 16, y = SCREEN_H - rise;
    gfx_round_fill(s, x + 2, y + 3, w, NOTE_H, 2, C_SHADOW);
    gfx_panel(s, x, y, w, NOTE_H, 2, C_STICKY, C_STICKY_SH);
    gfx_fill(s, x + 1, y + NOTE_H - 4, w - 2, 3, C_STICKY_SH);    // curled foot
    gfx_fill(s, SCREEN_W / 2 - 14, y - 3, 28, 7, C_PAPER_SH);     // tape
    if (note_meter)
    {
        int bx = gfx_text(s, x + 10, y + 12, meter_label, C_INK) + 8;
        int bw = x + w - 10 - bx;
        gfx_panel(s, bx, y + 9, bw, 13, 2, C_WHITE, C_INK_SOFT);
        int fill = (bw - 4) * meter_value / meter_max;
        if (fill > 0)
            gfx_round_fill(s, bx + 2, y + 11, fill, 9, 1, C_OK);
        return;
    }
    if (note_line[1][0])
    {
        gfx_text_center(s, SCREEN_W / 2, y + 6, note_line[0], C_INK);
        gfx_text_center(s, SCREEN_W / 2, y + 17, note_line[1], C_INK);
    }
    else
    {
        gfx_text_center(s, SCREEN_W / 2, y + 11, note_line[0], C_INK);
    }
}

// ---------------------------------------------------------------- stack

const Screen *ui_current(void)
{
    return depth > 0 ? stack[depth - 1] : NULL;
}

// Captures the pages on screen before the stack changes, once per frame.
static void turn_page(int dir)
{
    if (depth == 0)
        return;
    if (!snapped)
    {
        memcpy(old_top, g_top.buf, PIXELS);
        memcpy(old_bot, g_bot.buf, PIXELS);
        snapped = true;
    }
    turn_dir = dir;
    turn_left = TURN_FRAMES;
    swallow = true;
}

void ui_push(const Screen *s)
{
    undo_cancel();   // the offer belongs to the page it was made on
    if (depth >= UI_STACK_MAX)
        return;
    if (depth > 0)
        sfx_play(SFX_OPEN);
    turn_page(+1);
    stack[depth++] = s;
    head_pressed = HEAD_NONE;
    bool outer = pushing;   // enter() may push another page
    pushing = true;
    if (s->enter)
        s->enter();
    pushing = outer;
    ui_dirty();
}

bool ui_pushed(void)
{
    return pushing;
}

void ui_pop(void)
{
    undo_cancel();
    if (depth <= 1)
        return;
    sfx_play(SFX_BACK);
    turn_page(-1);
    const Screen *s = stack[--depth];
    if (s->leave)
        s->leave();
    head_pressed = HEAD_NONE;
    const Screen *top = ui_current();
    if (top->enter)
        top->enter();
    ui_dirty();
}

void ui_set_overlay(const Overlay *o)
{
    overlay = o;
}

void ui_init(const Screen *first)
{
    keysSetRepeat(20, 5);
    now_cache = time(NULL);
    ui_push(first);
}

// ---------------------------------------------------------------- pop-ups

#define SHEET_X 8
#define SHEET_W (SCREEN_W - 16)
#define SHEET_ROW 27

static int sheet_height(void)
{
    return 22 + m_n * SHEET_ROW + 5;
}

static void modal_rect(int i, int *x, int *y, int *w, int *h)
{
    if (modal == M_SHEET)
    {
        int top = SCREEN_H - sheet_height();
        *x = SHEET_X + 6;
        *w = SHEET_W - 12;
        *y = top + 22 + i * SHEET_ROW;
        *h = SHEET_ROW - 3;
    }
    else
    {
        *x = i == 0 ? 30 : 132;
        *w = 94;
        *y = 112;
        *h = 28;
    }
}

static int modal_count(void)
{
    return modal == M_SHEET ? m_n : 2;
}

static int modal_hit(int tx, int ty)
{
    for (int i = 0; i < modal_count(); i++)
    {
        int x, y, w, h;
        modal_rect(i, &x, &y, &w, &h);
        if (tx >= x && tx < x + w && ty >= y && ty < y + h)
            return i;
    }
    return -1;
}

static void modal_open(ModalKind kind)
{
    modal = kind;
    m_sel = 0;
    m_held = -1;
    m_frames = 0;
    modal_changed = true;
    swallow = true;
    sfx_play(SFX_POP);
}

void ui_sheet(const char *title, const SheetItem *items, int n, void (*done)(int index))
{
    snprintf(m_title[0], sizeof(m_title[0]), "%s", title ? title : "");
    m_n = n > SHEET_MAX ? SHEET_MAX : n;
    for (int i = 0; i < m_n; i++)
        m_items[i] = items[i];
    m_done = done;
    modal_open(M_SHEET);
}

void ui_confirm(const char *question, const char *detail, const char *yes, void (*on_yes)(void))
{
    wrap2(question, 24, m_title);
    snprintf(m_detail, sizeof(m_detail), "%s", detail ? detail : "");
    if ((int)strlen(detail ? detail : "") > 24)
        snprintf(m_detail, sizeof(m_detail), "%.23s~", detail);
    snprintf(m_yes_label, sizeof(m_yes_label), "%s", yes);
    m_yes = on_yes;
    modal_open(M_CONFIRM);
}

static void modal_close(void)
{
    modal = M_NONE;
    modal_changed = true;
    swallow = true;
}

static void modal_choose(int i)
{
    ModalKind kind = modal;
    modal_close();
    if (kind == M_SHEET)
    {
        if (m_done != NULL)
            m_done(i);
    }
    else if (i == 1)
    {
        if (m_yes != NULL)
            m_yes();
    }
    else
    {
        sfx_play(SFX_BACK);
    }
    ui_dirty();
}

static void modal_update(const Input *in)
{
    if (m_frames < 5)
    {
        m_frames++;
        modal_changed = true;
    }
    if (in->touch_down)
    {
        m_held = modal_hit(in->tx, in->ty);
        m_hot = m_held >= 0;
        if (m_held >= 0)
        {
            device_click();
        }
        else if (modal == M_SHEET && in->ty < SCREEN_H - sheet_height())
        {
            sfx_play(SFX_BACK);     // a tap outside the menu closes it
            modal_close();
            return;
        }
        modal_changed = true;
        return;
    }
    if (m_held >= 0)
    {
        if (in->touching)
        {
            bool hot = modal_hit(in->tx, in->ty) == m_held;
            if (hot != m_hot)
            {
                m_hot = hot;
                modal_changed = true;
            }
            return;
        }
        int i = m_hot && in->touch_up ? m_held : -1;
        m_held = -1;
        modal_changed = true;
        if (i >= 0)
            modal_choose(i);
        return;
    }
    int n = modal_count();
    uint32_t prev = modal == M_SHEET ? KEY_UP : KEY_LEFT;
    uint32_t next = modal == M_SHEET ? KEY_DOWN : KEY_RIGHT;
    if ((in->repeat & prev) && m_sel > 0)
        m_sel--, modal_changed = true;
    if ((in->repeat & next) && m_sel < n - 1)
        m_sel++, modal_changed = true;
    if (in->down & KEY_A)
    {
        modal_choose(m_sel);
    }
    else if (in->down & KEY_B)
    {
        sfx_play(SFX_BACK);
        modal_close();
    }
}

static void modal_draw(Surface *s)
{
    static const int SLIDE[6] = { 40, 22, 11, 5, 2, 0 };
    int off = SLIDE[m_frames > 5 ? 5 : m_frames];
    gfx_dither(s, 0, 0, SCREEN_W, SCREEN_H, C_BODY);
    if (modal == M_SHEET)
    {
        int h = sheet_height(), top = SCREEN_H - h + off;
        gfx_round_fill(s, SHEET_X, top, SHEET_W, h + 4, 4, C_INK);
        gfx_round_fill(s, SHEET_X + 1, top + 1, SHEET_W - 2, h + 4, 3, C_PAPER);
        gfx_text_center(s, SCREEN_W / 2, top + 8, m_title[0], C_INK_SOFT);
        for (int i = 0; i < m_n; i++)
        {
            int x, y, w, hh;
            modal_rect(i, &x, &y, &w, &hh);
            y += off;
            gfx_button(s, x, y, w, hh, m_items[i].label, m_items[i].icon, m_items[i].sec,
                       i == m_held && m_hot, true);
            if (pad_mode && i == m_sel)
            {
                gfx_frame(s, x - 2, y - 2, w + 4, hh + 4, C_ACCENT);
                gfx_pad_badge(s, x + w - 16, y + (hh - 13) / 2, 'A');
            }
        }
        return;
    }
    // Confirm: a sticky note in the middle of the page.
    int x = 20, y = 44 + off, w = 216, h = 104;
    gfx_round_fill(s, x + 3, y + 4, w, h, 3, C_BLACK);
    gfx_panel(s, x, y, w, h, 3, C_STICKY, C_STICKY_SH);
    gfx_fill(s, SCREEN_W / 2 - 16, y - 4, 32, 8, C_PAPER_SH);
    if (m_title[1][0])
    {
        gfx_text_center(s, SCREEN_W / 2, y + 13, m_title[0], C_INK);
        gfx_text_center(s, SCREEN_W / 2, y + 24, m_title[1], C_INK);
    }
    else
    {
        gfx_text_center(s, SCREEN_W / 2, y + 18, m_title[0], C_INK);
    }
    gfx_text_center(s, SCREEN_W / 2, y + 42, m_detail, C_INK_SOFT);
    for (int i = 0; i < 2; i++)
    {
        int bx, by, bw, bh;
        modal_rect(i, &bx, &by, &bw, &bh);
        by += off;
        gfx_button(s, bx, by, bw, bh, i == 0 ? "Keep" : m_yes_label, ICON_NONE,
                   i == 0 ? SEC_PLAIN : SEC_ALERT, i == m_held && m_hot, true);
        if (pad_mode && i == m_sel)
            gfx_frame(s, bx - 2, by - 2, bw + 4, bh + 4, C_ACCENT);
    }
}

// ---------------------------------------------------------------- drawing

static bool new_style(const Screen *s)
{
    return s->title != NULL || s->title_fn != NULL;
}

static bool has_back(const Screen *s)
{
    return depth > 1 && (!s->keep_b || s->back_touch);
}

// Header buttons, right to left: add, then the menu.
#define HEAD_BTN_Y 2
#define HEAD_BTN_H 20
#define HEAD_BACK_X 3
#define HEAD_BACK_W 34
#define HEAD_BTN_W 32

static int head_add_x(void)
{
    return SCREEN_W - 3 - HEAD_BTN_W;
}

// The right corner holds the trash (on_delete) or else "+" (on_add).
static bool head_right(const Screen *s)
{
    return s->on_delete != NULL || s->on_add != NULL;
}

static bool delete_ok(const Screen *s)
{
    return s->on_delete != NULL && (s->can_delete == NULL || s->can_delete());
}

static int head_more_x(const Screen *s)
{
    return head_right(s) ? head_add_x() - HEAD_BTN_W - 4 : head_add_x();
}

static int header_hit(const Screen *s, int tx, int ty)
{
    if (ty >= UI_HEAD_H)
        return HEAD_NONE;
    if (has_back(s) && tx < HEAD_BACK_X + HEAD_BACK_W + 4)
        return HEAD_BACK;
    if (head_right(s) && tx >= head_add_x() - 2)
        return s->on_delete ? HEAD_DEL : HEAD_ADD;
    if (s->on_more && tx >= head_more_x(s) - 2 && tx < head_more_x(s) + HEAD_BTN_W + 2)
        return HEAD_MORE;
    return HEAD_NONE;
}

static void draw_header(const Screen *s)
{
    Surface *b = &g_bot;
    gfx_fill(b, 0, 0, SCREEN_W, UI_HEAD_H, C_LEATHER);
    gfx_dotted_hline(b, 2, UI_HEAD_H - 3, SCREEN_W - 4, C_LEATHER_HI);
    gfx_hline(b, 0, UI_HEAD_H - 1, SCREEN_W, C_LEATHER_SH);
    gfx_rings(b, false);

    int x = 4;
    if (has_back(s))
    {
        gfx_button(b, HEAD_BACK_X, HEAD_BTN_Y, HEAD_BACK_W, HEAD_BTN_H, NULL, ICON_BACK,
                   SEC_PLAIN, head_pressed == HEAD_BACK, true);
        if (pad_mode && !s->keep_b)
            gfx_pad_badge(b, HEAD_BACK_X + HEAD_BACK_W - 7, HEAD_BTN_Y + HEAD_BTN_H - 9, 'B');
        x = HEAD_BACK_X + HEAD_BACK_W + 6;
    }
    // The section's index tab, standing on the page below.
    const char *title = s->title_fn ? s->title_fn() : s->title;
    int w = (int)strlen(title) * FONT_W + (s->icon ? 30 : 12);
    uint8_t tab = s->sec ? SEC_MID(s->sec) : C_PAPER;
    gfx_round_fill(b, x, 3, w, 8, 3, SEC_DK(s->sec));
    gfx_fill(b, x, 7, w, UI_HEAD_H - 7, SEC_DK(s->sec));
    gfx_round_fill(b, x + 1, 4, w - 2, 8, 2, tab);
    gfx_fill(b, x + 1, 8, w - 2, UI_HEAD_H - 8, tab);
    int tx = x + 6;
    if (s->icon)
    {
        gfx_icon(b, x + 4, 5, s->icon, 1);
        tx = x + 23;
    }
    gfx_text(b, tx, 9, title, C_INK);

    if (s->on_delete)
    {
        bool ok = delete_ok(s);
        gfx_button(b, head_add_x(), HEAD_BTN_Y, HEAD_BTN_W, HEAD_BTN_H, NULL, ICON_TRASH,
                   SEC_ALERT, ok && head_pressed == HEAD_DEL, ok);
    }
    else if (s->on_add)
    {
        gfx_button(b, head_add_x(), HEAD_BTN_Y, HEAD_BTN_W, HEAD_BTN_H, NULL, ICON_PLUS,
                   SEC_PLAIN, head_pressed == HEAD_ADD, true);
        if (pad_mode)
            gfx_pad_badge(b, head_add_x() + HEAD_BTN_W - 9, HEAD_BTN_Y + HEAD_BTN_H - 9, 'X');
    }
    if (s->on_more)
    {
        gfx_button(b, head_more_x(s), HEAD_BTN_Y, HEAD_BTN_W, HEAD_BTN_H, NULL,
                   s->more_icon ? s->more_icon : ICON_MORE, SEC_PLAIN,
                   head_pressed == HEAD_MORE, true);
        if (pad_mode)
            gfx_pad_badge(b, head_more_x(s) + HEAD_BTN_W - 9, HEAD_BTN_Y + HEAD_BTN_H - 9, 'Y');
    }
}

// The text editor: "<Back" and a hint line above its keyboard.
static void draw_status(const Screen *s)
{
    gfx_fill(&g_bot, 0, 0, SCREEN_W, 20, C_LEATHER);
    gfx_hline(&g_bot, 0, 19, SCREEN_W, C_LEATHER_SH);
    int x = 4, chars = 31;
    if (has_back(s))
    {
        gfx_key(&g_bot, 1, 1, UI_BACK_W - 2, 17, "<Back", false, true);
        x = UI_BACK_W + 4;
        chars = (SCREEN_W - x) / FONT_W;
    }
    const char *h = s->hint ? s->hint() : NULL;
    if (h != NULL)
        gfx_text_n(&g_bot, x, 6, h, chars, C_GOLD);
}

static bool overlay_active(void)
{
    return overlay != NULL && overlay->active();
}

static void redraw(bool top, bool bot)
{
    if (overlay_active())
    {
        overlay->draw(&g_top, &g_bot);
        return;
    }
    const Screen *s = ui_current();
    gfx_set_section(s->icon, s->sec);
    if (top && s->draw_top)
        s->draw_top(&g_top);
    if (bot)
    {
        if (s->draw_bottom)
            s->draw_bottom(&g_bot);
        if (new_style(s))
            draw_header(s);
        else
            draw_status(s);
    }
}

// The old page and the new one side by side, `shift` px into the turn.
static void slide(const uint8_t *from, const uint8_t *to, int shift, int dir)
{
    for (int y = 0; y < SCREEN_H; y++)
    {
        uint8_t *row = comp + y * SCREEN_W;
        const uint8_t *a = from + y * SCREEN_W, *b = to + y * SCREEN_W;
        if (dir > 0)
        {
            memcpy(row, a + shift, (size_t)(SCREEN_W - shift));
            memcpy(row + SCREEN_W - shift, b, (size_t)shift);
        }
        else
        {
            memcpy(row, b + SCREEN_W - shift, (size_t)shift);
            memcpy(row + shift, a, (size_t)(SCREEN_W - shift));
        }
    }
}

// Copies what changed to the screens, adding the page turn, the sticky
// note and the pop-up on top of the screen's own drawing.
static void present(void)
{
    static const int EASE[TURN_FRAMES] = { 256, 250, 240, 224, 198, 164, 120, 64 };
    bool plain = overlay_active();
    if (plain)
        turn_left = 0;
    int shift = turn_left > 0 ? EASE[turn_left - 1] : 0;

    int rise = plain ? 0 : note_rise();
    if (g_top.dirty || turn_left > 0 || rise != note_shown || (note_changed && rise > 0))
    {
        const uint8_t *src = g_top.buf;
        if (turn_left > 0)
        {
            slide(old_top, g_top.buf, shift, turn_dir);
            src = comp;
        }
        if (rise > 0)
        {
            if (src != comp)
                memcpy(comp, src, PIXELS);
            draw_note(&comp_surface, rise);
            src = comp;
        }
        gfx_blit(&g_top, src);
        g_top.dirty = false;
        note_shown = rise;
    }
    note_changed = false;

    bool pop = modal != M_NONE && !plain;
    if (g_bot.dirty || turn_left > 0 || modal_changed || undo_changed)
    {
        const uint8_t *src = g_bot.buf;
        if (turn_left > 0)
        {
            slide(old_bot, g_bot.buf, shift, turn_dir);
            src = comp;
        }
        if (undo_fn != NULL && !plain)
        {
            if (src != comp)
                memcpy(comp, src, PIXELS);
            gfx_round_fill(&comp_surface, UNDO_X + 2, UNDO_Y + 3, UNDO_W, UNDO_H, 3, C_SHADOW);
            gfx_button(&comp_surface, UNDO_X, UNDO_Y, UNDO_W, UNDO_H, "Undo", ICON_UNDO,
                       SEC_PLAIN, false, true);
            src = comp;
        }
        if (pop)
        {
            if (src != comp)
                memcpy(comp, src, PIXELS);
            modal_draw(&comp_surface);
            src = comp;
        }
        gfx_blit(&g_bot, src);
        g_bot.dirty = false;
    }
    modal_changed = false;
    undo_changed = false;
    if (turn_left > 0)
        turn_left--;
}

// ---------------------------------------------------------------- input

static void read_input(Input *in)
{
    static bool was_touching = false;
    static int last_x = -1, last_y = -1;
    scanKeys();
    in->down = keysDown();
    in->held = keysHeld();
    in->repeat = keysDownRepeat();
    in->touching = (in->held & KEY_TOUCH) != 0;
    in->touch_down = (in->down & KEY_TOUCH) != 0;
    in->touch_up = was_touching && !in->touching;
    in->tx = in->ty = -1;
    if (in->touching)
    {
        touchPosition tp;
        touchRead(&tp);
        in->tx = last_x = tp.px;
        in->ty = last_y = tp.py;
    }
    else if (in->touch_up)
    {
        in->tx = last_x;
        in->ty = last_y;
    }
    was_touching = in->touching;
}

static void no_touch(Input *in)
{
    in->touch_down = in->touching = in->touch_up = false;
    in->tx = in->ty = -1;
}

// The current screen's turn: back, the header and its buttons, then the
// screen itself.
static void screen_input(Input *in)
{
    const Screen *s = ui_current();
    bool back = false, acted = false;
    if (depth > 1 && !s->keep_b)
        back = (in->down & KEY_B) != 0;
    if (new_style(s))
    {
        int h = in->touch_down ? header_hit(s, in->tx, in->ty) : HEAD_NONE;
        if (in->touch_down && in->ty < UI_HEAD_H)
        {
            no_touch(in);
            swallow = true;
        }
        if (h == HEAD_DEL && !delete_ok(s))
            h = HEAD_NONE;   // greyed out: nothing picked to delete
        if (h == HEAD_NONE && s->on_add && (in->down & KEY_X))
            h = HEAD_ADD;
        else if (h == HEAD_NONE && s->on_more && (in->down & KEY_Y))
            h = HEAD_MORE;
        // X with the trash in the corner adds without a header key to light.
        if (h != HEAD_NONE && !(h == HEAD_ADD && s->on_delete))
        {
            head_pressed = h;
            head_frames = 6;
            dirty_bot = true;
        }
        if (h == HEAD_BACK)
        {
            back = true;
        }
        else if (h == HEAD_DEL)
        {
            device_click();
            s->on_delete();
            acted = true;
        }
        else if (h == HEAD_ADD)
        {
            device_click();
            s->on_add();
            acted = true;
        }
        else if (h == HEAD_MORE)
        {
            device_click();
            s->on_more();
            acted = true;
        }
    }
    else if (has_back(s) && in->touch_down && in->ty < 20 && in->tx < UI_BACK_W)
    {
        back = true;
    }
    if (back && s->on_back && s->on_back())
    {
        sfx_play(SFX_BACK);
        return;
    }
    if (back)
    {
        ui_pop();
        return;
    }
    if (acted)
        return;
    if (s->update)
        s->update(in);
}

void ui_frame(void)
{
    Input in;
    read_input(&in);
    device_filter_input(&in);
    now_cache = time(NULL);

    if (swallow)
    {
        if (!in.touching && !in.touch_up)
            swallow = false;
        no_touch(&in);
    }
    const uint32_t BUTTONS = KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R | KEY_UP | KEY_DOWN |
                             KEY_LEFT | KEY_RIGHT | KEY_START | KEY_SELECT;
    bool pad = pad_mode;
    if (in.down & BUTTONS)
        pad = true;
    else if (in.touch_down)
        pad = false;
    if (pad != pad_mode)
    {
        pad_mode = pad;
        dirty_bot = true;
        modal_changed = true;
    }

    if (overlay_active())
    {
        // Drawn in full when it appears; after that the overlay asks for a
        // redraw itself when its picture changes (not every frame).
        if (!overlay_was_active)
        {
            overlay_was_active = true;
            ui_dirty();
        }
        overlay->update(&in);
    }
    else
    {
        if (overlay_was_active)
        {
            overlay_was_active = false;
            swallow = true;   // the tap that stopped it is not for the page
            ui_dirty();
        }
        // The Undo key comes first: a tap on it (or SELECT) never reaches
        // the page under it.
        if (undo_fn != NULL && modal == M_NONE)
        {
            bool hit = (in.down & KEY_SELECT) ||
                       (in.touch_down && in.tx >= UNDO_X && in.tx < UNDO_X + UNDO_W &&
                        in.ty >= UNDO_Y && in.ty < UNDO_Y + UNDO_H);
            if (hit)
            {
                void (*fn)(void) = undo_fn;
                undo_cancel();
                no_touch(&in);
                in.down &= ~KEY_SELECT;
                swallow = true;
                sfx_play(SFX_SAVED);
                fn();
                ui_toast("Put back.");
                ui_dirty();
            }
            else if (--undo_frames <= 0)
            {
                undo_cancel();
            }
        }
        if (modal != M_NONE)
            modal_update(&in);
        else if (turn_left == 0)     // nothing is pressed while the page turns
            screen_input(&in);
    }

    if (head_frames > 0 && --head_frames == 0)
    {
        head_pressed = HEAD_NONE;
        dirty_bot = true;
    }
    if (note_frames > 0)
        note_frames--;

    const Screen *s = ui_current();
    if (now_cache != last_drawn_second)
    {
        // Clocks and countdowns are on the top screen. Header clocks show
        // minutes; a screen ticks every second only while it shows seconds.
        bool seconds = s->ticking != NULL && s->ticking();
        if (seconds || now_cache / 60 != last_drawn_second / 60)
            dirty_top = true;
        last_drawn_second = now_cache;
    }

    if (dirty_top || dirty_bot)
    {
        bool top = dirty_top, bot = dirty_bot;
        dirty_top = dirty_bot = false;
        redraw(top, bot);
    }
    present();
    snapped = false;
}
