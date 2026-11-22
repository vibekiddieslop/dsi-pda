// Handwriting pad: write one printed letter at a time with the stylus.
//
// A letter may take several strokes. It is recognized half a second after
// the stylus lifts, or at once when the next stroke starts clearly to the
// right of it. The strip above the pad shows the other likely characters;
// tapping one replaces the letter just written. A long flat stroke to the
// right is a space, to the left a backspace. A single dot is a period.
#include <stdlib.h>
#include <string.h>

#include <nds.h>

#include "app.h"
#include "device.h"
#include "input/hwr.h"
#include "textin.h"

#define STRIP_Y 20
#define STRIP_H 20
#define PAD_Y 40
#define PAD_H 118
#define BTN_Y 160
#define BTN_H 32
#define COMMIT_FRAMES 30     // pen-up pause that ends a letter
#define NEXT_LETTER_GAP 12   // px right of the letter that starts a new one
#define CAND_MAX 4

enum { MODE_ABC, MODE_CAPS, MODE_123 };
enum { B_MODE, B_KEYS, B_SPACE, B_BKSP, B_ENTER, B_COUNT };
static const short BTN_X[B_COUNT + 1] = { 0, 44, 92, 164, 210, 256 };

static InkPt ink[HWR_MAX_POINTS];
static int ink_n = 0, strokes = 0;
static bool drawing = false;
static int idle = 0;
static int max_x;            // right edge of the letter being written
static int mode = MODE_ABC;

static char cand[CAND_MAX];
static int ncand = 0;
static bool cand_upper = false;

static int pressed_btn = -1, pressed_frames = 0, held_btn = -1;
static bool sentence_hint = true;   // the mode key shows "Abc" when true

void pad_init(void)
{
    hwr_init();
}

void pad_reset(const TextSink *t)
{
    (void)t;
    ink_n = strokes = 0;
    drawing = false;
    ncand = 0;
    // An amount opens in digit mode; the next text entry goes back to
    // letters unless digits were picked by hand.
    static bool forced_123 = false;
    if (textin_numbers())
    {
        if (mode != MODE_123)
            forced_123 = true;
        mode = MODE_123;
    }
    else if (forced_123)
    {
        forced_123 = false;
        mode = MODE_ABC;
    }
    if (mode == MODE_CAPS)
        mode = MODE_ABC;
}

static bool upper_now(const TextSink *t)
{
    return mode == MODE_CAPS || (mode == MODE_ABC && textin_sentence_start(t));
}

static char cased(char c, bool upper)
{
    return (upper && c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static void clear_ink(void)
{
    ink_n = strokes = 0;
    idle = 0;
    ui_dirty_bottom();
}

void pad_text_edited(const TextSink *t)
{
    (void)t;
    ncand = 0;   // The last letter may be gone or the cursor elsewhere.
    ui_dirty_bottom();
}

// The ink of the letter just read, kept until the next one, so a pick
// from the strip can teach the recognizer what that ink meant.
static InkPt last_ink[HWR_MAX_POINTS];
static int last_n = 0;
static bool learned = false;   // a sample was already taken from it

static void commit(const TextSink *t)
{
    if (ink_n == 0)
        return;
    char g = strokes == 1 ? hwr_gesture(ink, ink_n, SCREEN_W) : 0;
    if (g == ' ')
    {
        t->insert(" ");
        ncand = 0;
    }
    else if (g == '\b')
    {
        t->backspace(1);
        ncand = 0;
    }
    else
    {
        ncand = hwr_recognize(ink, ink_n, mode == MODE_123 ? HWR_DIGITS : HWR_LETTERS, cand,
                              CAND_MAX);
        if (ncand > 0)
        {
            memcpy(last_ink, ink, sizeof(InkPt) * (size_t)ink_n);
            last_n = ink_n;
            learned = false;
            cand_upper = upper_now(t);
            char s[2] = { cased(cand[0], cand_upper), 0 };
            if (t->insert(s) == 0)
            {
                // Text is full: nothing went in, so there is nothing a
                // candidate could replace.
                ncand = 0;
                ui_toast("Text full.");
            }
        }
    }
    clear_ink();
}

static void add_point(int x, int y)
{
    if (ink_n > 0 && ink[ink_n - 1].stroke == strokes)
    {
        int dx = x - ink[ink_n - 1].x, dy = y - ink[ink_n - 1].y;
        if (dx * dx + dy * dy < 4)
            return;
    }
    if (ink_n >= HWR_MAX_POINTS)
        return;
    ink[ink_n++] = (InkPt){ (short)x, (short)y, (unsigned char)strokes };
    if (ink_n == 1 || x > max_x)
        max_x = x;
}

static int button_at(int x, int y)
{
    if (y < BTN_Y || y >= BTN_Y + BTN_H)
        return -1;
    for (int b = 0; b < B_COUNT; b++)
        if (x >= BTN_X[b] && x < BTN_X[b + 1])
            return b;
    return -1;
}

static void press_button(const TextSink *t, int b)
{
    device_click();
    pressed_btn = b;
    pressed_frames = 6;
    if (b == B_BKSP && ink_n > 0)
    {
        clear_ink();   // Del on an unfinished letter just erases it.
        return;
    }
    commit(t);   // A letter in progress is finished first.
    switch (b)
    {
        case B_MODE: mode = (mode + 1) % 3; break;
        case B_KEYS: t->switch_mode(); return;
        case B_SPACE: t->insert(" "); ncand = 0; break;
        case B_BKSP: t->backspace(1); ncand = 0; break;
        case B_ENTER: t->enter(); return;
    }
    ui_dirty_bottom();   // Text changes redraw the top through the editor.
}

void pad_update(const Input *in, const TextSink *t)
{
    bool sh = mode == MODE_ABC && textin_sentence_start(t);
    if (sh != sentence_hint)
    {
        sentence_hint = sh;
        ui_dirty_bottom();
    }
    if (pressed_frames > 0 && --pressed_frames == 0)
    {
        pressed_btn = -1;
        ui_dirty_bottom();
    }

    if (in->touch_down)
    {
        if (in->ty >= STRIP_Y && in->ty < PAD_Y)
        {
            // Swap the last letter for another candidate.
            int i = in->tx * CAND_MAX / SCREEN_W;
            if (i > 0 && i < ncand)
            {
                device_click();
                t->backspace(1);
                char s[2] = { cased(cand[i], cand_upper), 0 };
                t->insert(s);
                char c = cand[0];
                cand[0] = cand[i];
                cand[i] = c;
                // Learn from the correction: this ink was that letter. A
                // second pick takes the first one back.
                if (learned)
                    hwr_user_drop_last();
                learned = last_n > 0 && hwr_user_add(cand[0], last_ink, last_n);
            }
            return;
        }
        int b = button_at(in->tx, in->ty);
        if (b >= 0)
        {
            held_btn = b;
            press_button(t, b);
            return;
        }
        if (in->ty >= PAD_Y && in->ty < PAD_Y + PAD_H)
        {
            if (strokes > 0 && in->tx > max_x + NEXT_LETTER_GAP)
                commit(t);
            if (strokes >= HWR_MAX_STROKES)
                commit(t);
            drawing = true;
            add_point(in->tx, in->ty);
            ui_dirty_bottom();
        }
        return;
    }

    if (in->touching)
    {
        if (drawing)
        {
            int y = in->ty < PAD_Y ? PAD_Y : in->ty >= PAD_Y + PAD_H ? PAD_Y + PAD_H - 1 : in->ty;
            int before = ink_n;
            add_point(in->tx, y);
            if (ink_n != before)
                ui_dirty_bottom();   // Only new ink needs a redraw.
        }
        else if (held_btn == B_BKSP && button_at(in->tx, in->ty) == B_BKSP &&
                 (in->repeat & KEY_TOUCH))
        {
            device_click();
            t->backspace(1);
            ncand = 0;
        }
        return;
    }

    held_btn = -1;
    if (drawing)
    {
        drawing = false;
        strokes++;
        idle = 0;
        return;
    }
    if (strokes > 0 && ++idle >= COMMIT_FRAMES)
        commit(t);
}

static const char *button_label(int b, bool sentence)
{
    switch (b)
    {
        case B_MODE:
            return mode == MODE_123 ? "123" : mode == MODE_CAPS ? "ABC" : sentence ? "Abc" : "abc";
        case B_KEYS: return "Keys";
        case B_SPACE: return "space";
        case B_BKSP: return "Del";
        case B_ENTER: return "Enter";
    }
    return "";
}

void pad_draw(Surface *s)
{
    gfx_fill(s, 0, STRIP_Y, SCREEN_W, SCREEN_H - STRIP_Y, C_BODY);

    // Candidates for the last letter.
    gfx_fill(s, 0, STRIP_Y, SCREEN_W, STRIP_H, C_BODY_HI);
    if (ncand > 0)
    {
        for (int i = 0; i < ncand; i++)
        {
            int x = i * SCREEN_W / CAND_MAX, w = SCREEN_W / CAND_MAX;
            if (i == 0)
                gfx_fill(s, x + 1, STRIP_Y + 1, w - 2, STRIP_H - 2, C_SEL);
            if (i > 0)
                gfx_vline(s, x, STRIP_Y + 3, STRIP_H - 6, C_DIM);
            char c[2] = { cased(cand[i], cand_upper), 0 };
            gfx_text_center(s, x + w / 2, STRIP_Y + 6, c, i == 0 ? C_INK : C_LABEL);
        }
    }
    else
    {
        gfx_text_center(s, SCREEN_W / 2, STRIP_Y + 6, "One letter at a time", C_DIM);
    }

    // Writing area with guide lines like a ruled page.
    gfx_fill(s, 2, PAD_Y, SCREEN_W - 4, PAD_H, C_WHITE);
    gfx_dotted_hline(s, 6, PAD_Y + PAD_H * 3 / 4, SCREEN_W - 12, C_RULE);
    gfx_dotted_hline(s, 6, PAD_Y + PAD_H * 3 / 8, SCREEN_W - 12, C_RULE);
    if (ink_n == 0)
    {
        const char *tip = mode == MODE_123 ? "write a digit" : "write a letter";
        gfx_text_center(s, SCREEN_W / 2, PAD_Y + PAD_H / 2 - 4, tip, C_DIM);
        gfx_text_center(s, SCREEN_W / 2, PAD_Y + PAD_H - 12, "--> space   <-- delete",
                        C_RULE);
    }
    for (int i = 1; i < ink_n; i++)
    {
        if (ink[i].stroke == ink[i - 1].stroke)
            gfx_line(s, ink[i - 1].x, ink[i - 1].y, ink[i].x, ink[i].y, C_INK, 2);
    }
    if (ink_n == 1 || (ink_n > 1 && ink[ink_n - 1].stroke != ink[ink_n - 2].stroke))
        gfx_fill(s, ink[ink_n - 1].x - 1, ink[ink_n - 1].y - 1, 2, 2, C_INK);

    for (int b = 0; b < B_COUNT; b++)
        gfx_key(s, BTN_X[b], BTN_Y, BTN_X[b + 1] - BTN_X[b], BTN_H,
                button_label(b, sentence_hint), b == pressed_btn, true);
}

// ---------------------------------------------------------------- samples

// The user's handwriting samples live in pda/HWR.TXT (crash-safe like every
// PDA file, with HWR.BAK).
void handwriting_load(void)
{
    if (!app_storage_ok())
        return;
    char p[PDA_PATH_MAX];
    app_path("HWR.TXT", p, sizeof(p));
    char *text = NULL;
    size_t len = 0;
    text_load(p, 512 * 1024, &text, &len);
    if (text != NULL)
    {
        hwr_user_parse(text);
        free(text);
    }
}

void handwriting_save(void)
{
    if (!hwr_user_dirty() || !app_storage_ok())
        return;
    char p[PDA_PATH_MAX];
    app_path("HWR.TXT", p, sizeof(p));
    TextBuf b;
    textbuf_init(&b);
    if (!hwr_user_serialize(&b) || !text_save(p, b.data ? b.data : "", b.len))
        ui_toast("Save failed: HWR.TXT");
    textbuf_free(&b);
}
