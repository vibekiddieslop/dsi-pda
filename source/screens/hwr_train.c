// Train handwriting (Settings): teaches the pad the user's own way of
// writing each letter, digit and sign.
//
// Training goes through 68 prompts: small letters, capitals, digits, then
// ? ! - + / :. The top screen shows the one to write, big, and how many of
// the user's samples it has; the bottom screen is a writing pad. After each
// stroke the top screen says what the pad reads, so the user sees where it
// struggles. Save keeps the ink as a sample and moves on.
//
// Try it: write anything and see the guesses. A confirms the first; a tap
// on another guess, or Other to type it, teaches what was meant.
//
// Samples are templates for the recognizer (input/hwr.c), saved as
// pda/HWR.TXT when the page closes.
#include <stdio.h>

#include <nds.h>

#include "device.h"
#include "input/hwr.h"
#include "screens.h"
#include "sfx.h"
#include "ui/editor.h"
#include "ui/widgets.h"

#define STRIP_Y UI_HEAD_H            // guesses
#define STRIP_H 20
#define PAD_Y (STRIP_Y + STRIP_H)    // the writing area
#define PAD_H (UI_BAR_Y - 2 - PAD_Y)
#define GUESS_MAX 4

typedef struct {
    char ch;            // what is stored (lowercase for letters)
    char shown;         // what is asked for
    const char *kind;
} Prompt;

#define PROMPT_COUNT (26 + 26 + 10 + 6)
static Prompt prompts[PROMPT_COUNT];
static int cur = 0;
static bool test_mode = false;
static int test_set = HWR_LETTERS;

static InkPt ink[HWR_MAX_POINTS];
static int ink_n = 0, strokes = 0;
static bool drawing = false;
static char guess[GUESS_MAX];
static int nguess = 0;
static int saved_frames = 0;   // "Saved!" shows this long
static Bar bar;

static void build_prompts(void)
{
    static const struct {
        char c;
        const char *kind;
    } SIGNS[6] = {
        { '?', "question mark" }, { '!', "exclamation" }, { '-', "minus" },
        { '+', "plus" },          { '/', "slash" },        { ':', "colon" },
    };
    int n = 0;
    for (char c = 'a'; c <= 'z'; c++)
        prompts[n++] = (Prompt){ c, c, "small letter" };
    for (char c = 'a'; c <= 'z'; c++)
        prompts[n++] = (Prompt){ c, (char)(c - 32), "capital letter" };
    for (char c = '0'; c <= '9'; c++)
        prompts[n++] = (Prompt){ c, c, "digit" };
    for (int i = 0; i < 6; i++)
        prompts[n++] = (Prompt){ SIGNS[i].c, SIGNS[i].c, SIGNS[i].kind };
}

static const Prompt *prompt(void)
{
    return &prompts[cur];
}

// ---------------------------------------------------------------- ink

static void clear_ink(void)
{
    ink_n = strokes = 0;
    drawing = false;
    nguess = 0;
    ui_dirty();
}

static void add_point(int x, int y)
{
    if (ink_n > 0 && ink[ink_n - 1].stroke == strokes)
    {
        int dx = x - ink[ink_n - 1].x, dy = y - ink[ink_n - 1].y;
        if (dx * dx + dy * dy < 4)
            return;
    }
    if (ink_n < HWR_MAX_POINTS)
        ink[ink_n++] = (InkPt){ (short)x, (short)y, (unsigned char)strokes };
}

// What the pad makes of the ink so far.
static void read_ink(void)
{
    int set = test_mode ? test_set : hwr_set_of(prompt()->ch);
    nguess = ink_n > 0 ? hwr_recognize(ink, ink_n, (HwrSet)set, guess, GUESS_MAX) : 0;
    ui_dirty();
}

// A guess as the page shows it: for a capital prompt, the capital.
static char shown_guess(char g)
{
    if (!test_mode && prompt()->shown >= 'A' && prompt()->shown <= 'Z' && g >= 'a' && g <= 'z')
        return (char)(g - 32);
    return g;
}

// ---------------------------------------------------------------- actions

static void go(int d)
{
    int next = cur + d;
    if (next >= PROMPT_COUNT)
    {
        next = 0;
        ui_toast("Round 2! More samples help.");
    }
    if (next < 0)
        next = PROMPT_COUNT - 1;
    cur = next;
    clear_ink();
}

static void save_sample(void)
{
    if (ink_n == 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Write it first.");
        return;
    }
    if (!hwr_user_add(prompt()->ch, ink, ink_n))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Too small. Write bigger.");
        clear_ink();
        return;
    }
    sfx_play(SFX_STAMP);
    saved_frames = 50;
    go(+1);
}

// Try it: the ink was `c`. Teaches it unless it already reads that way.
static void teach(char c)
{
    if (c >= 'A' && c <= 'Z')
        c = (char)(c + 32);
    if (hwr_set_of(c) < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Only letters, digits, ? ! - + / :");
        return;
    }
    if (ink_n == 0)
        return;
    bool was_right = nguess > 0 && guess[0] == c;
    if (!hwr_user_add(c, ink, ink_n))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Too small. Write bigger.");
        return;
    }
    sfx_play(SFX_STAMP);
    if (was_right)
        ui_toast("Right! Saved as %c.", c);
    else
        ui_toast("Learned: %c.", c);
    // Its set may differ from the one being read (a digit while reading
    // letters): read that one next.
    test_set = hwr_set_of(c);
    clear_ink();
}

static void other_done(bool ok, const char *text)
{
    if (ok && text[0] != '\0')
        teach(text[0]);
}

static void ask_other(void)
{
    if (ink_n == 0)
    {
        ui_toast("Write something first.");
        return;
    }
    editor_open("WHAT WAS IT?", "What did you write?", "", 1, other_done);
}

static void goto_done(bool ok, const char *text)
{
    if (!ok || text[0] == '\0')
        return;
    char c = text[0];
    for (int i = 0; i < PROMPT_COUNT; i++)
        if (prompts[i].shown == c)
        {
            test_mode = false;
            cur = i;
            clear_ink();
            return;
        }
    sfx_play(SFX_ERROR);
    ui_toast("Cannot train: %c", c);
}

static char forget_ch;

static void forget_yes(void)
{
    hwr_user_forget(forget_ch);
    sfx_play(SFX_DELETE);
    if (forget_ch)
        ui_toast("Forgot %c.", forget_ch);
    else
        ui_toast("Forgot all.");
    ui_dirty();
}

static void forget_this(void)
{
    forget_ch = prompt()->ch;
    int n = hwr_user_count(forget_ch);
    if (n == 0)
    {
        ui_toast("No samples: %c", prompt()->shown);
        return;
    }
    static char q[40];
    snprintf(q, sizeof(q), "Forget %d sample%s of %c?", n, n == 1 ? "" : "s", forget_ch);
    ui_confirm(q, "Capital and small.", "Forget", forget_yes);
}

static void forget_all(void)
{
    forget_ch = 0;
    int n = hwr_user_total();
    if (n == 0)
    {
        ui_toast("No samples.");
        return;
    }
    static char q[40];
    snprintf(q, sizeof(q), "Forget all %d samples?", n);
    ui_confirm(q, "The pad starts over.", "Forget", forget_yes);
}

static int menu_map[4];

static void more_done(int i)
{
    switch (menu_map[i])
    {
        case 0:
            test_mode = !test_mode;
            clear_ink();
            break;
        case 1:
            editor_open("GO TO", "Character? (A, a, 7, ?)", "", 1, goto_done);
            break;
        case 2: forget_this(); break;
        case 3: forget_all(); break;
    }
}

static void do_more(void)
{
    SheetItem items[4];
    int n = 0;
    items[n] = (SheetItem){ test_mode ? "Back to training" : "Try it", ICON_PLAY, SEC_PLAIN };
    menu_map[n++] = 0;
    items[n] = (SheetItem){ "Go to character", ICON_NEXT, SEC_PLAIN };
    menu_map[n++] = 1;
    if (!test_mode)
    {
        items[n] = (SheetItem){ "Forget this character", ICON_UNDO, SEC_PLAIN };
        menu_map[n++] = 2;
    }
    items[n] = (SheetItem){ "Forget all", ICON_TRASH, SEC_ALERT };
    menu_map[n++] = 3;
    ui_sheet("Handwriting", items, n, more_done);
}

// ---------------------------------------------------------------- input

static void update(const Input *in)
{
    if (saved_frames > 0 && --saved_frames == 0)
        ui_dirty_top();

    int b = bar_update(&bar, in);
    if (b >= 0)
    {
        if (test_mode)
        {
            if (b == 0)
                clear_ink();
            else if (b == 1)
            {
                test_set = test_set == HWR_LETTERS ? HWR_DIGITS : HWR_LETTERS;
                read_ink();
            }
            else
                ask_other();
        }
        else
        {
            if (b == 0)
                clear_ink();
            else if (b == 1)
                go(+1);
            else
                save_sample();
        }
        return;
    }

    if (in->touch_down)
    {
        if (in->ty >= STRIP_Y && in->ty < PAD_Y)
        {
            // Try it: the guess that was meant teaches it.
            int i = in->tx * GUESS_MAX / SCREEN_W;
            if (test_mode && i < nguess)
            {
                device_click();
                teach(guess[i]);
            }
            return;
        }
        if (in->ty >= PAD_Y && in->ty < PAD_Y + PAD_H && strokes < HWR_MAX_STROKES)
        {
            drawing = true;
            saved_frames = 0;
            add_point(in->tx, in->ty);
            ui_dirty_bottom();
        }
        return;
    }
    if (in->touching && drawing)
    {
        int y = in->ty < PAD_Y ? PAD_Y : in->ty >= PAD_Y + PAD_H ? PAD_Y + PAD_H - 1 : in->ty;
        int before = ink_n;
        add_point(in->tx, y);
        if (ink_n != before)
            ui_dirty_bottom();
        return;
    }
    if (drawing)
    {
        // A stroke ended: read what is there so far.
        drawing = false;
        strokes++;
        read_ink();
        return;
    }

    if (in->down & KEY_X)
        clear_ink();
    if (test_mode)
    {
        if ((in->down & KEY_A) && nguess > 0)
            teach(guess[0]);
        return;
    }
    if (in->down & KEY_A)
        save_sample();
    if (in->repeat & KEY_R)
        go(+1);
    if (in->repeat & KEY_L)
        go(-1);
}

// ---------------------------------------------------------------- drawing

static void sample_dots(Surface *s, int x, int y, int n)
{
    for (int i = 0; i < HWR_USER_PER_CHAR; i++)
    {
        int dx = x + i * 11;
        if (i < n)
            gfx_round_fill(s, dx, y, 8, 8, 3, C_OK);
        else
            gfx_panel(s, dx, y, 8, 8, 3, C_PAPER, C_INK_SOFT);
    }
}

static void draw_top(Surface *s)
{
    char buf[48];
    if (test_mode)
    {
        gfx_header(s, "Handwriting", "try it");
        gfx_paper(s, PAPER_Y);
        gfx_text(s, 20, PAPER_TEXT(0),
                 test_set == HWR_LETTERS ? "Reading letters" : "Reading digits and signs",
                 C_INK_SOFT);
        if (nguess > 0)
        {
            char big[2] = { guess[0], 0 };
            gfx_text_scaled(s, 136 - 24, PAPER_Y + 16, big, C_INK, 6);
            snprintf(buf, sizeof(buf), "It reads: %c", guess[0]);
            gfx_text_center(s, 136, PAPER_TEXT(6), buf, C_ACCENT);
            if (nguess > 1)
            {
                int n = snprintf(buf, sizeof(buf), "or:");
                for (int i = 1; i < nguess; i++)
                    n += snprintf(buf + n, sizeof(buf) - (size_t)n, " %c", guess[i]);
                gfx_text_center(s, 136, PAPER_TEXT(7), buf, C_INK_SOFT);
            }
            gfx_text(s, 20, PAPER_TEXT(8), "Right? Press A.", C_INK_SOFT);
            gfx_text(s, 20, PAPER_TEXT(9), "Wrong? Tap the right one,", C_INK_SOFT);
            gfx_text(s, 20, PAPER_TEXT(10), "or Other to type it.", C_INK_SOFT);
            gfx_text(s, 20, PAPER_TEXT(11), "It learns from that.", C_INK_SOFT);
        }
        else
        {
            gfx_text_center(s, 136, PAPER_TEXT(4), "Write below.", C_INK);
            gfx_text_center(s, 136, PAPER_TEXT(5), "See what it reads.", C_INK);
            snprintf(buf, sizeof(buf), "%d sample%s of yours so far", hwr_user_total(),
                     hwr_user_total() == 1 ? "" : "s");
            gfx_text_center(s, 136, PAPER_TEXT(7), buf, C_INK_SOFT);
        }
        static const char *const KEYS[] = { "A", "Right", "X", "Clear", "Y", "Menu", NULL };
        legend_draw(s, PAPER_TEXT(13), KEYS);
        return;
    }

    const Prompt *p = prompt();
    snprintf(buf, sizeof(buf), "%d of %d", cur + 1, PROMPT_COUNT);
    gfx_header(s, "Handwriting", buf);
    gfx_paper(s, PAPER_Y);
    gfx_text(s, 20, PAPER_TEXT(0), "Write this:", C_INK_SOFT);
    // The character to write, big, on plain paper.
    gfx_fill(s, 17, PAPER_Y + PAPER_ROW, SCREEN_W - 17, 4 * PAPER_ROW, C_PAPER);
    char big[2] = { p->shown, 0 };
    gfx_text_scaled(s, 136 - 24, PAPER_Y + PAPER_ROW + 1, big, C_INK, 6);
    gfx_text_center(s, 136, PAPER_TEXT(5), p->kind, C_INK_SOFT);

    int n = hwr_user_count(p->ch);
    int x = gfx_text(s, 20, PAPER_TEXT(7), "Yours:", C_INK_SOFT) + 6;
    sample_dots(s, x, PAPER_TEXT(7), n);

    if (saved_frames > 0)
    {
        gfx_text(s, 20, PAPER_TEXT(9), "Saved. Next one.", C_OK);
    }
    else if (nguess > 0)
    {
        char g = shown_guess(guess[0]);
        bool right = guess[0] == p->ch;
        snprintf(buf, sizeof(buf), "It reads: %c", g);
        gfx_text(s, 20, PAPER_TEXT(9), buf, right ? C_OK : C_ALERT);
        gfx_text(s, 20, PAPER_TEXT(10),
                 right ? "Save to confirm." : "Save to teach it.", C_INK_SOFT);
    }
    else
    {
        gfx_text(s, 20, PAPER_TEXT(9), "Write it your way.", C_INK_SOFT);
    }
    snprintf(buf, sizeof(buf), "%d sample%s of yours in all", hwr_user_total(),
             hwr_user_total() == 1 ? "" : "s");
    gfx_text(s, 20, PAPER_TEXT(12), buf, C_INK_SOFT);
    static const char *const KEYS[] = { "A", "Save", "X", "Redo", "R", "Skip", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

static void draw_bottom(Surface *s)
{
    // The guesses, like the pad's strip.
    gfx_fill(s, 0, STRIP_Y, SCREEN_W, STRIP_H, C_BODY_HI);
    if (nguess > 0)
    {
        for (int i = 0; i < nguess; i++)
        {
            int x = i * SCREEN_W / GUESS_MAX, w = SCREEN_W / GUESS_MAX;
            if (i == 0)
                gfx_fill(s, x + 1, STRIP_Y + 1, w - 2, STRIP_H - 2, C_SEL);
            if (i > 0)
                gfx_vline(s, x, STRIP_Y + 3, STRIP_H - 6, C_DIM);
            char c[2] = { shown_guess(guess[i]), 0 };
            gfx_text_center(s, x + w / 2, STRIP_Y + 6, c, i == 0 ? C_INK : C_LABEL);
        }
    }
    else
    {
        gfx_text_center(s, SCREEN_W / 2, STRIP_Y + 6,
                        test_mode ? "Tap the right one" : "Guesses show here", C_DIM);
    }

    // The writing area, ruled like the pad.
    gfx_fill(s, 0, PAD_Y, SCREEN_W, UI_BAR_Y - PAD_Y, C_BODY);
    gfx_fill(s, 2, PAD_Y, SCREEN_W - 4, PAD_H, C_WHITE);
    gfx_dotted_hline(s, 6, PAD_Y + PAD_H * 3 / 4, SCREEN_W - 12, C_RULE);
    gfx_dotted_hline(s, 6, PAD_Y + PAD_H * 3 / 8, SCREEN_W - 12, C_RULE);
    if (ink_n == 0 && !test_mode)
    {
        // A faint reminder of what to write, out of the way in the corner.
        char hint[2] = { prompt()->shown, 0 };
        gfx_text(s, 8, PAD_Y + 4, hint, C_RULE);
    }
    for (int i = 1; i < ink_n; i++)
        if (ink[i].stroke == ink[i - 1].stroke)
            gfx_line(s, ink[i - 1].x, ink[i - 1].y, ink[i].x, ink[i].y, C_INK, 2);
    if (ink_n == 1 || (ink_n > 1 && ink[ink_n - 1].stroke != ink[ink_n - 2].stroke))
        gfx_fill(s, ink[ink_n - 1].x - 1, ink[ink_n - 1].y - 1, 2, 2, C_INK);

    bar.n = 3;
    if (test_mode)
    {
        bar.item[0] = (BarItem){ "Clear", ICON_UNDO, SEC_PLAIN, ink_n > 0 };
        bar.item[1] = (BarItem){ test_set == HWR_LETTERS ? "abc" : "123", ICON_NONE,
                                 SEC_PLAIN, true };
        bar.item[2] = (BarItem){ "Other...", ICON_EDIT, SEC_PLAIN, ink_n > 0 };
    }
    else
    {
        bar.item[0] = (BarItem){ "Redo", ICON_UNDO, SEC_PLAIN, ink_n > 0 };
        bar.item[1] = (BarItem){ "Skip", ICON_NEXT, SEC_PLAIN, true };
        bar.item[2] = (BarItem){ "Save", ICON_CHECK, SEC_TODO, ink_n > 0 };
    }
    bar_draw(s, &bar);
}

// ---------------------------------------------------------------- screen

static void enter(void)
{
    static bool built = false;
    if (!built)
    {
        build_prompts();
        built = true;
    }
    device_cpu_fast(true);   // reading handwriting is real work
}

static void leave(void)
{
    device_cpu_fast(false);
    handwriting_save();
    clear_ink();
}

const Screen SCR_HWR_TRAIN = {
    .enter = enter,
    .leave = leave,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title = "Handwriting",
    .icon = ICON_EDIT,
    .sec = SEC_SETTINGS,
    .on_more = do_more,
};
