// Sketch: draw with the stylus on the bottom screen. Saved as a 16-color BMP
// (pda/NOTES/Sxxxx.BMP) when the sketch closes, so it opens on a PC too.
//
// Bottom screen: header, 256x140 canvas, then the tools, each one tap:
//   four inks, the eraser, three pen sizes, undo.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <nds.h>

#include "app.h"
#include "screens.h"
#include "sfx.h"
#include "store/bmp_io.h"
#include "store/notes_io.h"
#include "ui/widgets.h"

#define CANVAS_Y UI_HEAD_H
#define CANVAS_W SKETCH_W
#define CANVAS_H SKETCH_H
#define UNDO_MAX 4

// Tools along the bottom edge.
enum { T_INK0, T_INK1, T_INK2, T_INK3, T_ERASE, T_SIZE0, T_SIZE1, T_SIZE2, T_UNDO, T_COUNT };

// Sketch colors: index in the BMP file, screen palette entry, file color.
typedef struct {
    uint8_t screen;
    uint32_t rgb;
    const char *name;
} Ink;
static const Ink INKS[] = {
    { C_PAPER, 0xF4ECD0, "paper" },
    { C_INK, 0x282838, "black" },
    { C_ACCENT, 0x3060A0, "blue" },
    { C_ALERT, 0xC03030, "red" },
    { C_OK, 0x388840, "green" },
};
#define INK_COUNT ((int)(sizeof(INKS) / sizeof(INKS[0])))
static const int SIZES[3] = { 1, 2, 4 };   // brush radius in px
static const char *SIZE_NAMES[3] = { "fine", "medium", "thick" };
#define ERASER_RADIUS 6

static uint8_t canvas[CANVAS_H][CANVAS_W];        // screen palette entries
static uint8_t undo[UNDO_MAX][CANVAS_H][CANVAS_W];
static int undo_n = 0, undo_top = 0;

static int sketch_id = 0;
static char title[NOTE_TITLE_MAX + 1];
static int size_i = 1, ink_i = 1;
static bool erasing = false;
static bool drawing = false, changed = false;
// The power button and a flat battery end the PDA without a Back, so a
// changed sketch is also saved once the stylus has been up a while.
#define AUTOSAVE_FRAMES 180
static int idle_frames = 0;
static int last_x, last_y;
static Press press;

static void file_path(char *out, int size)
{
    sketch_path(app_notes_dir(), sketch_id, out, (size_t)size);
}

static void clear_canvas(void)
{
    memset(canvas, C_PAPER, sizeof(canvas));
}

// Reads sketch `id` into dst as screen palette entries. False when there
// is no file or it is not a PDA sketch (dst is then blank paper).
bool sketch_read(int id, uint8_t *dst)
{
    memset(dst, C_PAPER, (size_t)CANVAS_W * CANVAS_H);
    char p[PDA_PATH_MAX];
    sketch_path(app_notes_dir(), id, p, sizeof(p));
    size_t len = 0;
    char *data = text_read_file(p, 64 * 1024, &len);
    if (data == NULL)
        return false;
    static uint8_t pix[CANVAS_H * CANVAS_W];
    bool ok = bmp4_decode((const uint8_t *)data, len, pix, CANVAS_W, CANVAS_H);
    if (ok)
    {
        for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
            dst[i] = INKS[pix[i] < INK_COUNT ? pix[i] : 1].screen;
    }
    free(data);
    return ok;
}

static int ink_index(uint8_t screen)
{
    for (int i = 0; i < INK_COUNT; i++)
        if (INKS[i].screen == screen)
            return i;
    return 1;
}

static void save(void)
{
    if (!changed || !app_storage_ok())
        return;
    static uint8_t pix[CANVAS_H * CANVAS_W];
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            pix[y * CANVAS_W + x] = (uint8_t)ink_index(canvas[y][x]);
    uint32_t colors[INK_COUNT];   // not "rgb": libnds has a type of that name
    for (int i = 0; i < INK_COUNT; i++)
        colors[i] = INKS[i].rgb;
    size_t len = 0;
    uint8_t *file = bmp4_encode(pix, CANVAS_W, CANVAS_H, colors, INK_COUNT, &len);
    char p[PDA_PATH_MAX];
    file_path(p, sizeof(p));
    bool ok = file != NULL && text_save(p, (const char *)file, len);
    free(file);
    if (ok)
        changed = false;
    else
        ui_toast("Save failed: S%04d.BMP", sketch_id);
}

static void push_undo(void)
{
    memcpy(undo[undo_top], canvas, sizeof(canvas));
    undo_top = (undo_top + 1) % UNDO_MAX;
    if (undo_n < UNDO_MAX)
        undo_n++;
}

static void do_undo(void)
{
    if (undo_n == 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Nothing to undo.");
        return;
    }
    undo_top = (undo_top + UNDO_MAX - 1) % UNDO_MAX;
    undo_n--;
    memcpy(canvas, undo[undo_top], sizeof(canvas));
    changed = true;
    sfx_play(SFX_UNCHECK);
    ui_dirty_bottom();
}

static void stamp(int cx, int cy)
{
    int r = erasing ? ERASER_RADIUS : SIZES[size_i];
    uint8_t c = erasing ? C_PAPER : INKS[ink_i].screen;
    for (int dy = -r; dy <= r; dy++)
    {
        int y = cy + dy;
        if (y < 0 || y >= CANVAS_H)
            continue;
        for (int dx = -r; dx <= r; dx++)
        {
            int x = cx + dx;
            if (x >= 0 && x < CANVAS_W && dx * dx + dy * dy <= r * r + r)
                canvas[y][x] = c;
        }
    }
}

static void stroke_to(int x1, int y1)
{
    int x0 = last_x, y0 = last_y;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;)
    {
        stamp(x0, y0);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    last_x = x1;
    last_y = y1;
    changed = true;
}

// ---------------------------------------------------------------- tools

static void tool_rects(Rect *r)
{
    for (int t = 0; t < T_COUNT; t++)
        r[t] = (Rect){ (short)(2 + t * 28), UI_BAR_Y + 4, 26, UI_BAR_H - 7 };
}

static void use_tool(int t)
{
    if (t >= T_INK0 && t <= T_INK3)
    {
        erasing = false;
        ink_i = t - T_INK0 + 1;   // skip paper
    }
    else if (t == T_ERASE)
    {
        erasing = !erasing;
    }
    else if (t >= T_SIZE0 && t <= T_SIZE2)
    {
        erasing = false;
        size_i = t - T_SIZE0;
    }
    else if (t == T_UNDO)
    {
        do_undo();
    }
    ui_dirty();
}

static void clear_yes(void)
{
    push_undo();
    clear_canvas();
    changed = true;
    sfx_play(SFX_DELETE);
    ui_toast("Cleared. Undo brings it back.");
    ui_dirty();
}

static void more_done(int i)
{
    if (i == 0)
        do_undo();
    else
        ui_confirm("Clear sketch?", "Undo brings it back.", "Clear", clear_yes);
}

static void do_more(void)
{
    static const SheetItem items[2] = {
        { "Undo", ICON_UNDO, SEC_PLAIN },
        { "Clear", ICON_TRASH, SEC_ALERT },
    };
    ui_sheet("Sketch", items, 2, more_done);
}

static void update(const Input *in)
{
    if (in->touching)
        idle_frames = 0;
    else if (changed && ++idle_frames == AUTOSAVE_FRAMES)
        save();

    Rect r[T_COUNT];
    tool_rects(r);
    int t = press_update(&press, in, r, T_COUNT);
    if (t >= 0)
        use_tool(t);

    if (in->touch_down)
    {
        if (in->ty >= CANVAS_Y && in->ty < CANVAS_Y + CANVAS_H)
        {
            push_undo();
            drawing = true;
            last_x = in->tx;
            last_y = in->ty - CANVAS_Y;
            stroke_to(last_x, last_y);
            ui_dirty_bottom();
        }
        return;
    }
    if (in->touching && drawing)
    {
        int y = in->ty - CANVAS_Y;
        y = y < 0 ? 0 : y >= CANVAS_H ? CANVAS_H - 1 : y;
        if (in->tx != last_x || y != last_y)
        {
            stroke_to(in->tx, y);
            ui_dirty_bottom();
        }
        return;
    }
    drawing = false;

    // The shoulder buttons sit under the hand that is not drawing.
    if (in->repeat & KEY_L)
        do_undo();
    if (in->down & KEY_R)
        use_tool(T_INK0 + ink_i % (INK_COUNT - 1));   // the next ink
    if (in->down & KEY_A)
        use_tool(T_ERASE);
}

static void draw_top(Surface *s)
{
    char buf[48];
    gfx_header(s, "Sketch", NULL);
    gfx_paper(s, PAPER_Y);
    int row = title_draw(s, title, 0, C_INK) + 1;

    int x = gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "PEN", SEC_MID(SEC_NOTES), C_INK);
    if (erasing)
        snprintf(buf, sizeof(buf), "eraser");
    else
        snprintf(buf, sizeof(buf), "%s, %s", SIZE_NAMES[size_i], INKS[ink_i].name);
    gfx_text(s, x + 5, PAPER_TEXT(row), buf, erasing ? C_INK_SOFT : INKS[ink_i].screen);
    row += 2;
    gfx_text(s, 20, PAPER_TEXT(row), "Draw below.", C_INK_SOFT);
    gfx_text(s, 20, PAPER_TEXT(row + 1), "Go back to save.", C_INK_SOFT);

    static const char *const KEYS[] = { "L", "Undo", "R", "Ink", "A", "Eraser", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

static void draw_bottom(Surface *s)
{
    for (int y = 0; y < CANVAS_H; y++)
        memcpy(s->buf + (CANVAS_Y + y) * SCREEN_W, canvas[y], CANVAS_W);
    s->dirty = true;

    gfx_fill(s, 0, UI_BAR_Y, SCREEN_W, UI_BAR_H, C_LEATHER);
    gfx_hline(s, 0, UI_BAR_Y, SCREEN_W, C_LEATHER_SH);
    Rect r[T_COUNT];
    tool_rects(r);
    for (int t = 0; t < T_COUNT; t++)
    {
        bool on = (t >= T_INK0 && t <= T_INK3 && !erasing && ink_i == t - T_INK0 + 1) ||
                  (t == T_ERASE && erasing) ||
                  (t >= T_SIZE0 && t <= T_SIZE2 && !erasing && size_i == t - T_SIZE0);
        bool down = on || press_is(&press, t);
        int x = r[t].x, y = r[t].y, w = r[t].w, h = r[t].h;
        gfx_button(s, x, y, w, h, NULL, t == T_ERASE ? ICON_ERASER : t == T_UNDO ? ICON_UNDO : 0,
                   on ? SEC_NOTES : SEC_PLAIN, down, true);
        int cx = x + w / 2, cy = y + h / 2 - (down ? 0 : 1);
        if (t >= T_INK0 && t <= T_INK3)
            gfx_round_fill(s, cx - 6, cy - 6, 12, 12, 4, INKS[t - T_INK0 + 1].screen);
        if (t >= T_SIZE0 && t <= T_SIZE2)
        {
            int rad = SIZES[t - T_SIZE0] + 1;
            gfx_round_fill(s, cx - rad, cy - rad, 2 * rad, 2 * rad, rad > 4 ? 4 : rad,
                           INKS[ink_i].screen);
        }
    }
}

static const char *sketch_title(void)
{
    static char t[20];
    snprintf(t, sizeof(t), "%.14s", title);
    return t;
}

static void leave(void)
{
    save();
}

const Screen SCR_SKETCH = {
    .leave = leave,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title_fn = sketch_title,
    .icon = ICON_SKETCH,
    .sec = SEC_NOTES,
    .on_more = do_more,
};

void sketch_open(int id, const char *t)
{
    sketch_id = id;
    snprintf(title, sizeof(title), "%s", t);
    undo_n = undo_top = 0;
    drawing = false;
    erasing = false;
    idle_frames = 0;
    // A new sketch is saved even if left blank, so it shows in the list.
    char p[PDA_PATH_MAX];
    file_path(p, sizeof(p));
    struct stat st;
    changed = stat(p, &st) != 0;
    if (!sketch_read(id, &canvas[0][0]) && !changed)
        ui_toast("S%04d.BMP: not a sketch.", id);
    ui_push(&SCR_SKETCH);
}
