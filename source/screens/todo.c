// To-Do. Bottom screen: the list. Tap a box to check it off, tap a row to
// pick it, tap it again to edit. The bar below acts on the picked item.
// Top screen: the picked item in full.
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "app.h"
#include "screens.h"
#include "sfx.h"
#include "ui/editor.h"
#include "ui/widgets.h"

#define BOX_W 26

static List list;
static Bar bar;
static bool reorder = false;              // the picked row is being moved
static int stamp_item = -1, stamp_frames = 0;   // check mark coming down

static void save(void)
{
    if (!app_save_todo())
        ui_toast("%s", app_last_error());
    list.count = g_todo.count;
    list_fix(&list);
    ui_dirty();
}

static bool any(void)
{
    return g_todo.count > 0;
}

// "today", "tmrw", "Fri", "3d!" (late) or "11-28". Returns the chip's
// colors through face and ink.
static void due_chip(const TodoItem *it, Date today, char *out, int size, uint8_t *face,
                     uint8_t *ink)
{
    long days = date_to_days(it->due) - date_to_days(today);
    if (days == 0)
        snprintf(out, (size_t)size, "today");
    else if (days == 1)
        snprintf(out, (size_t)size, "tmrw");
    else if (days > 1 && days < 7)
        snprintf(out, (size_t)size, "%s", DATE_DAY3[date_weekday(it->due)]);
    else if (days < 0 && days > -100 && !it->done)
        snprintf(out, (size_t)size, "%ldd!", -days);   // days late
    else
        snprintf(out, (size_t)size, "%02d-%02d", it->due.m, it->due.d);
    *face = SEC_LT(SEC_TIMERS);
    *ink = C_ACCENT;
    if (it->done)
    {
        *face = C_PAPER_SH;
        *ink = C_INK_SOFT;
    }
    else if (days < 0)
    {
        *face = SEC_MID(SEC_ALERT);
        *ink = C_SNOW;
    }
    else if (days == 0)
    {
        *face = SEC_MID(SEC_HABITS);
        *ink = C_INK;
    }
}

// ---------------------------------------------------------------- top

static void draw_top(Surface *s)
{
    char buf[64];
    int open = todo_open_count(&g_todo);
    snprintf(buf, sizeof(buf), "%d open", open);
    gfx_header(s, "To-Do", buf);
    gfx_paper(s, PAPER_Y);

    if (!any())
    {
        gfx_icon(s, 112, 54, ICON_SUN, 2);
        gfx_text_center(s, 136, PAPER_TEXT(7), "No to-dos.", C_INK);
        gfx_text_center(s, 136, PAPER_TEXT(8), "Tap Add.", C_INK_SOFT);
        return;
    }
    list.count = g_todo.count;
    list_fix(&list);
    // The picked to-do, in full.
    snprintf(buf, sizeof(buf), "%d of %d", list.sel + 1, g_todo.count);
    todo_detail_draw(s, &g_todo.items[list.sel], today_date(), buf);

    // How the whole list is going.
    int done = g_todo.count - open;
    snprintf(buf, sizeof(buf), "%d of %d done", done, g_todo.count);
    gfx_text(s, 20, PAPER_TEXT(10), buf, done == g_todo.count ? C_OK : C_INK_SOFT);
    int by = PAPER_Y + 11 * PAPER_ROW + 1;
    gfx_panel(s, 20, by, 216, 9, 2, C_WHITE, C_INK_SOFT);
    if (done > 0)
        gfx_round_fill(s, 22, by + 2, 212 * done / g_todo.count, 5, 1, SEC_MID(SEC_TODO));

    static const char *const KEYS[] = { "A", "Check", "X", "Add", "Y", "Menu", NULL };
    static const char *const MOVE_KEYS[] = { "A", "Done", NULL };
    if (reorder)
        gfx_text(s, 20, PAPER_TEXT(12), "Up and Down move it.", C_ACCENT);
    legend_draw(s, PAPER_TEXT(13), reorder ? MOVE_KEYS : KEYS);
}

// ---------------------------------------------------------------- bottom

static void draw_row(Surface *s, int i, int y, bool selected)
{
    const TodoItem *it = &g_todo.items[i];
    gfx_check_box(s, 6, y + 2);
    if (it->done)
    {
        int size = i == stamp_item && stamp_frames > 0 ? 7 + stamp_frames * 2 : 7;
        gfx_stamp_check(s, 13, y + 9, size, C_OK);
    }

    int right = SCREEN_W - 8;   // text stops here
    if (reorder && selected)
    {
        gfx_icon(s, right - 16, y + 2, ICON_REORDER, 1);
        right -= 20;
    }
    else if (it->has_due)
    {
        char due[8];
        uint8_t face, ink;
        due_chip(it, today_date(), due, sizeof(due), &face, &ink);
        int w = (int)strlen(due) * FONT_W + 7;
        gfx_chip(s, right - w, y + 4, due, face, ink);
        right -= w + 4;
    }
    if (it->repeat && !(reorder && selected))
    {
        gfx_icon(s, right - 16, y + 2, ICON_REPEAT, 1);
        right -= 18;
    }
    int x = BOX_W + 6;
    int end = gfx_text_n(s, x, y + 6, it->text, (right - x) / FONT_W,
                         it->done ? C_INK_SOFT : C_INK);
    if (it->done)
        gfx_hline(s, x, y + 10, end - x, C_INK_SOFT);
}

static void draw_bottom(Surface *s)
{
    list.count = g_todo.count;
    list_draw(&list, s, draw_row);
    if (!any())
    {
        gfx_text_center(s, SCREEN_W / 2, 70, "List empty.", C_INK_SOFT);
        gfx_text_center(s, SCREEN_W / 2, 86, "Tap Add.", C_INK_SOFT);
    }
    if (reorder)
    {
        bar.n = 3;
        bar.item[0] = (BarItem){ "Up", ICON_UP, SEC_PLAIN, list.sel > 0 };
        bar.item[1] = (BarItem){ "Down", ICON_DOWN, SEC_PLAIN, list.sel < g_todo.count - 1 };
        bar.item[2] = (BarItem){ "Done", ICON_CHECK, SEC_TODO, true };
    }
    else
    {
        bar.n = 3;
        bar.item[0] = (BarItem){ "Edit", ICON_EDIT, SEC_PLAIN, any() };
        bar.item[1] = (BarItem){ "Date", ICON_CAL, SEC_PLAIN, any() };
        bar.item[2] = (BarItem){ "Add", ICON_PLUS, SEC_TODO, true };
    }
    bar_draw(s, &bar);
}

// ---------------------------------------------------------------- actions

// The item the When screen is editing.
static int when_item = -1;

static void when_done(const WhenValue *v)
{
    if (when_item < 0 || when_item >= g_todo.count)
        return;
    TodoItem *it = &g_todo.items[when_item];
    it->has_due = v->has_due;
    it->due = v->due;
    it->repeat = v->repeat;
    it->every = v->every;
    list_show(&list, when_item);
    sfx_play(SFX_SAVED);
    save();
}

static void do_date(void)
{
    if (!any())
        return;
    const TodoItem *it = &g_todo.items[list.sel];
    WhenValue v = { it->has_due, it->due, it->repeat, it->every };
    if (!it->has_due)
        v.due = today_date();
    when_item = list.sel;
    when_open(it->text, &v, when_done);
}

// Adding is one step: type it, press Enter. The Date key on the bar sets a
// day afterwards; typing "due:fri" at the end works too.
static void add_done(bool ok, const char *typed)
{
    if (!ok)
        return;
    char text[TODO_TEXT_MAX + 40];
    todo_expand_typed(typed, text, sizeof(text));
    int i = todo_add(&g_todo, text);
    if (i < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast(g_todo.count >= TODO_MAX ? "List full." : "Nothing typed.");
        return;
    }
    list.count = g_todo.count;
    list_show(&list, i);
    sfx_play(SFX_SAVED);
    save();
}

static void do_add(void)
{
    reorder = false;
    editor_open("NEW TO-DO", "To-do text?", "", TODO_TEXT_MAX + 15, add_done);
}

static void edit_done(bool ok, const char *typed)
{
    if (!ok || list.sel >= g_todo.count)
        return;
    char text[TODO_TEXT_MAX + 40];
    todo_expand_typed(typed, text, sizeof(text));
    if (!todo_edit_text(&g_todo.items[list.sel], text))
    {
        sfx_play(SFX_ERROR);
        ui_toast("No text. Use the trash.");
        return;
    }
    sfx_play(SFX_SAVED);
    save();
}

static void do_edit(void)
{
    if (!any())
        return;
    // Only the text: the Date key changes the date and repeat. Typing due:
    // or every: here still replaces them.
    editor_open("EDIT TO-DO", "Edit the text.", g_todo.items[list.sel].text,
                TODO_TEXT_MAX + 15, edit_done);
}

static void do_toggle(int i)
{
    if (i < 0 || i >= g_todo.count)
        return;
    TodoItem *it = &g_todo.items[i];
    if (it->done)
    {
        it->done = false;
        sfx_play(SFX_UNCHECK);
    }
    else if (todo_complete(it, today_date()))
    {
        // A repeating to-do moves on instead of being crossed out.
        sfx_play(SFX_STAMP);
        ui_toast("Done! Next: %s %d %s", DATE_DAY3[date_weekday(it->due)], it->due.d,
                 DATE_MONTH3[it->due.m - 1]);
    }
    else
    {
        sfx_play(SFX_STAMP);
        stamp_item = i;
        stamp_frames = 6;
        if (todo_open_count(&g_todo) == 0)
            ui_toast("All done. Good job!");
    }
    save();
}

// Undo after a delete: the whole list as it was (TODO_MAX items, kept
// in RAM only until the Undo key goes away).
static TodoList undo_list;
static int undo_sel;

static void todo_undo(void)
{
    g_todo = undo_list;
    list.count = g_todo.count;
    list_show(&list, undo_sel);
    save();
}

static void snapshot(void)
{
    undo_list = g_todo;
    undo_sel = list.sel;
}

static void delete_yes(void)
{
    if (list.sel >= g_todo.count)
        return;
    snapshot();
    todo_remove(&g_todo, list.sel);
    sfx_play(SFX_DELETE);
    save();
    ui_undo_offer(todo_undo);
}

static void do_delete(void)
{
    if (any())
        ui_confirm("Delete to-do?", g_todo.items[list.sel].text, "Delete", delete_yes);
}

// Removes every checked item. The previous list stays in TODO.BAK, as with
// every save.
static void clear_yes(void)
{
    snapshot();
    int w = 0;
    for (int r = 0; r < g_todo.count; r++)
        if (!g_todo.items[r].done)
            g_todo.items[w++] = g_todo.items[r];
    int removed = g_todo.count - w;
    g_todo.count = w;
    list.sel = 0;
    list.scroll = 0;
    sfx_play(SFX_DELETE);
    save();
    ui_toast("Removed %d to-do%s.", removed, removed == 1 ? "" : "s");
    ui_undo_offer(todo_undo);
}

static void do_clear_done(void)
{
    int done = g_todo.count - todo_open_count(&g_todo);
    if (done == 0)
    {
        ui_toast("None checked.");
        return;
    }
    char q[40];
    snprintf(q, sizeof(q), "Remove %d to-do%s?", done, done == 1 ? "" : "s");
    ui_confirm(q, "Backup kept: TODO.BAK", "Remove", clear_yes);
}

// Dated items by date, then undated, then checked (order kept inside each
// group). The selection follows the same item.
static void do_sort(void)
{
    if (!any())
        return;
    TodoItem keep = g_todo.items[list.sel];
    todo_sort_by_due(&g_todo);
    for (int i = 0; i < g_todo.count; i++)
    {
        const TodoItem *it = &g_todo.items[i];
        if (it->done == keep.done && it->has_due == keep.has_due &&
            (!it->has_due || date_cmp(it->due, keep.due) == 0) &&
            strcmp(it->text, keep.text) == 0)
        {
            list_show(&list, i);
            break;
        }
    }
    save();
    ui_toast("Sorted.");
}

static void do_move(int dir)
{
    int n = todo_move(&g_todo, list.sel, dir);
    if (n != list.sel)
    {
        list_show(&list, n);
        save();
    }
}

// ---------------------------------------------------------------- menu

static char clear_label[32];

static void more_done(int i)
{
    switch (i)
    {
        case 0: do_edit(); break;
        case 1: do_date(); break;
        case 2: do_delete(); break;
        case 3: do_sort(); break;
        case 4: do_clear_done(); break;
        case 5:
            reorder = true;
            ui_dirty();
            break;
    }
}

static void do_more(void)
{
    if (!any())
    {
        ui_toast("Nothing here. Tap Add.");
        return;
    }
    reorder = false;
    int done = g_todo.count - todo_open_count(&g_todo);
    snprintf(clear_label, sizeof(clear_label), "Clear checked (%d)", done);
    const SheetItem items[6] = {
        { "Edit", ICON_EDIT, SEC_PLAIN },          { "Date and repeat", ICON_CAL, SEC_PLAIN },
        { "Delete", ICON_TRASH, SEC_ALERT },       { "Sort by date", ICON_SORT, SEC_PLAIN },
        { clear_label, ICON_CHECK, SEC_PLAIN },    { "Reorder", ICON_REORDER, SEC_PLAIN },
    };
    ui_sheet("To-Do", items, 6, more_done);
}

// ---------------------------------------------------------------- input

static void update(const Input *in)
{
    if (stamp_frames > 0)
    {
        stamp_frames--;
        ui_dirty_bottom();
    }
    list.count = g_todo.count;
    int b = bar_update(&bar, in);

    if (reorder)
    {
        // Up and Down carry the picked row; a tap can still pick another.
        if (b == 0 || (in->repeat & KEY_UP))
            do_move(-1);
        else if (b == 1 || (in->repeat & KEY_DOWN))
            do_move(+1);
        else if (b == 2 || (in->down & KEY_A))
        {
            reorder = false;
            sfx_play(SFX_SAVED);
            ui_dirty();
        }
        Input touch = *in;
        touch.down = touch.repeat = 0;
        list_update(&list, &touch);
        return;
    }

    if (b == 0)
        do_edit();
    else if (b == 1)
        do_date();
    else if (b == 2)
        do_add();
    if (b >= 0)
        return;

    ListEvent ev = list_update(&list, in);
    if (ev.kind == LE_BOX)
        do_toggle(ev.index);
    else if (ev.kind == LE_OPEN)
        do_edit();
}

// B first leaves Reorder, then the screen.
static bool on_back(void)
{
    if (!reorder)
        return false;
    reorder = false;
    ui_dirty();
    return true;
}

static const char *title(void)
{
    return reorder ? "Reorder" : "To-Do";
}

static void enter(void)
{
    if (list.height == 0)
        list_init(&list, UI_HEAD_H, UI_BAR_Y - UI_HEAD_H, BOX_W);
    list.count = g_todo.count;
    list_fix(&list);
}

static void leave(void)
{
    reorder = false;
}

const Screen SCR_TODO = {
    .enter = enter,
    .leave = leave,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title_fn = title,
    .icon = ICON_TODO,
    .sec = SEC_TODO,
    .on_add = do_add,
    .on_more = do_more,
    .on_back = on_back,
    .on_delete = do_delete,
    .can_delete = any,
};
