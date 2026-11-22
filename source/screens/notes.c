// Notes: the list of notes and sketches, and the note itself, line by line.
// Bottom screen: the list; tap to pick, tap again to open (or edit a line).
// Top screen: a preview of the picked note or sketch, or the note as it
// reads.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <nds.h>

#include "app.h"
#include "screens.h"
#include "sfx.h"
#include "store/notes_io.h"
#include "ui/editor.h"
#include "ui/widgets.h"

#define BOX_W 26

static NoteIndex idx;
static List list;
static Bar bar;

// Open note, checklist or bills.
static int open_id = 0;
enum { DOC_NOTE, DOC_LIST, DOC_BILLS };
static const char DOC_LETTER[3] = { 'N', 'L', 'B' };
static int open_kind = DOC_NOTE;
static bool open_list = false;    // Lxxxx.TXT: line 0 the title, then items
static bool open_bills = false;   // Bxxxx.TXT: line 0 the title, then bills
static NoteDoc doc;
static List lines;
static Bar line_bar;
static int line_scroll = 0;   // top screen: first display row shown

// Reading the index opens every note for its title, so it is redone only
// after something that can change it (a save, a new note or sketch).
static bool index_stale = true;

// Preview of the picked entry on the top screen, read a moment after the
// selection settles so scrolling stays quick.
#define PV_ROWS 11
#define PV_DELAY 10
static int pv_id = -1, pv_wait = 0;
static bool pv_sketch = false, pv_ok = false;
static int pv_kind = DOC_NOTE;
static char pv_total[TEXT_WRAP_W + 1];   // bills: the total line, or ""
static char pv_text[PV_ROWS][TEXT_WRAP_W + 1];
static int8_t pv_box[PV_ROWS];   // checklist rows: 0 open, 1 done; -1 no box
static int pv_n = 0;
static uint8_t pv_pix[SKETCH_W * SKETCH_H];   // a sketch's drawing

// The file of a note, checklist or bills.
static void doc_path(int id, int kind, char *out, size_t size)
{
    if (kind == DOC_LIST)
        checklist_path(app_notes_dir(), id, out, size);
    else if (kind == DOC_BILLS)
        bills_path(app_notes_dir(), id, out, size);
    else
        note_path(app_notes_dir(), id, out, size);
}

static void reload_index(void)
{
    if (!app_storage_ok() || !note_index_load(&idx, app_notes_dir()))
        idx.count = 0;
    index_stale = false;
    pv_id = -1;
    list.count = idx.count;
    list_fix(&list);
}

static bool rows_stale;

static bool save_doc(void)
{
    index_stale = true;   // the title (first line) may have changed
    rows_stale = true;
    char p[PDA_PATH_MAX];
    doc_path(open_id, open_kind, p, sizeof(p));
    TextBuf b;
    textbuf_init(&b);
    bool ok = app_storage_ok() && note_doc_to_text(&doc, &b) &&
              text_save(p, b.data ? b.data : "", b.len);
    textbuf_free(&b);
    if (!ok)
        ui_toast("Save failed: %c%04d.TXT", DOC_LETTER[open_kind], open_id);
    lines.count = doc.count;
    list_fix(&lines);
    ui_dirty();
    return ok;
}

#define LINE_BOX_W 26   // check box zone of a checklist's rows

#define TOTAL_H 20      // the bills' total row above the action bar

// The kind of an entry in the index.
static int kind_of(const NoteInfo *n)
{
    return n->checklist ? DOC_LIST : n->bills ? DOC_BILLS : DOC_NOTE;
}

// Sets up the line list for a note (no boxes), a checklist (boxes) or
// bills (a total row below the list).
static void lines_mode(int kind)
{
    open_kind = kind;
    open_list = kind == DOC_LIST;
    open_bills = kind == DOC_BILLS;
    lines.box_w = open_list ? LINE_BOX_W : 0;
    lines.height = UI_BAR_Y - UI_HEAD_H - (open_bills ? TOTAL_H : 0);
}

static bool open_note(int id, int kind)
{
    char p[PDA_PATH_MAX];
    doc_path(id, kind, p, sizeof(p));
    char *text = NULL;
    size_t len;
    LoadResult r = text_load(p, NOTE_MAX_BYTES, &text, &len);
    if (r == LOAD_ERROR)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Note unreadable.");
        return false;
    }
    note_doc_free(&doc);
    note_doc_init(&doc);
    if (text != NULL)
    {
        note_doc_from_text(&doc, text);
        free(text);
    }
    if (r == LOAD_FROM_BAK)
        ui_toast("Backup loaded: %c%04d.BAK", DOC_LETTER[kind], id);
    lines_mode(kind);
    open_id = id;
    rows_stale = true;
    lines.sel = lines.scroll = 0;
    line_scroll = 0;
    ui_push(&SCR_NOTE_VIEW);
    return true;
}

// ---------------------------------------------------------------- preview

// "Rent ........ $1,200.00" in `width` characters: the name cut to fit,
// the amount at the right.
static void bill_row_text(const char *name, long long cents, int width, char *out,
                          size_t size)
{
    char amount[24];
    money_format(cents, amount, sizeof(amount));
    int al = (int)strlen(amount);
    int room = width - al - 1;
    if (room < 0)
        room = 0;
    int nl = (int)strlen(name);
    if (nl > room)
        nl = room;
    snprintf(out, size, "%.*s%*s%s", nl, name, width - nl - al, "", amount);
}

static void preview_load(void)
{
    pv_n = 0;
    pv_ok = false;
    pv_total[0] = '\0';
    long long total = 0;
    if (list.sel >= idx.count)
        return;
    const NoteInfo *n = &idx.n[list.sel];
    pv_id = n->id;
    pv_sketch = n->sketch;
    pv_kind = kind_of(n);
    if (n->sketch)
    {
        pv_ok = sketch_read(n->id, pv_pix);
        return;
    }
    char p[PDA_PATH_MAX];
    doc_path(n->id, kind_of(n), p, sizeof(p));
    char *text = NULL;
    size_t len;
    if (text_load(p, NOTE_MAX_BYTES, &text, &len) == LOAD_ERROR || text == NULL)
        return;
    pv_ok = true;
    // Everything after the title line, wrapped; a checklist item per row.
    char *line = strchr(text, '\n');
    while (line != NULL && (pv_n < PV_ROWS || n->bills))
    {
        line++;
        char *end = strchr(line, '\n');
        if (end != NULL)
            *end = '\0';
        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\r')
            line[l - 1] = '\0';
        if (line[0] == '\0' && end == NULL)
            break;
        if (n->checklist)
        {
            bool done;
            const char *item = checklist_item(line, &done);
            if (item[0] != '\0' || line[0] == '[')
            {
                snprintf(pv_text[pv_n], sizeof(pv_text[0]), "%.26s", item);
                pv_box[pv_n++] = done;
            }
        }
        else if (n->bills)
        {
            // Every bill counts toward the total; the first ones are shown,
            // with two rows kept for the total.
            if (line[0] != '\0')
            {
                char name[NOTE_LINE_MAX + 1];
                long c;
                bill_item(line, name, sizeof(name), &c);
                total += c;
                if (pv_n < PV_ROWS - 2)
                {
                    bill_row_text(name, c, 29, pv_text[pv_n], sizeof(pv_text[0]));
                    pv_box[pv_n++] = -1;
                }
            }
        }
        else if (line[0] == '\0')
        {
            pv_box[pv_n] = -1;
            pv_text[pv_n++][0] = '\0';
        }
        else
        {
            int k = text_wrap(line, 29, &pv_text[pv_n], PV_ROWS - pv_n);
            for (int j = 0; j < k; j++)
                pv_box[pv_n + j] = -1;
            pv_n += k;
        }
        line = end;
    }
    free(text);
    if (n->bills)
        bill_row_text("TOTAL", total, 29, pv_total, sizeof(pv_total));
}

// Asks for a fresh preview when the picked entry is not the one shown.
static void preview_check(void)
{
    if (idx.count == 0 || list.sel >= idx.count)
        return;
    const NoteInfo *n = &idx.n[list.sel];
    if (n->id == pv_id && n->sketch == pv_sketch && kind_of(n) == pv_kind)
        return;
    pv_id = n->id;
    pv_sketch = n->sketch;
    pv_kind = kind_of(n);
    pv_ok = false;
    pv_n = 0;
    pv_wait = PV_DELAY;
}

// ---------------------------------------------------------------- list

static void list_draw_top(Surface *s)
{
    char right[16];
    snprintf(right, sizeof(right), "%d", idx.count);
    gfx_header(s, "Notes", right);
    gfx_paper(s, PAPER_Y);
    if (idx.count == 0)
    {
        gfx_icon(s, 112, 54, ICON_NOTES, 2);
        gfx_text_center(s, 136, PAPER_TEXT(7), "No notes.", C_INK);
        gfx_text_center(s, 136, PAPER_TEXT(8), "Tap Add.", C_INK_SOFT);
        return;
    }
    list_fix(&list);
    const NoteInfo *n = &idx.n[list.sel];
    int row = title_draw(s, n->title, 0, C_ACCENT);
    if (n->sketch)
    {
        // The drawing itself, under its title.
        if (pv_ok && pv_wait == 0)
        {
            int y0 = PAPER_Y + row * PAPER_ROW;
            int h = SCREEN_H - 6 - y0 < SKETCH_H ? SCREEN_H - 6 - y0 : SKETCH_H;
            for (int y = 0; y < h; y++)
                memcpy(s->buf + (y0 + y) * SCREEN_W, pv_pix + y * SKETCH_W, SKETCH_W);
            gfx_hline(s, 0, y0, SCREEN_W, C_RULE);
        }
        return;
    }
    for (int i = 0; i < pv_n && row < PAPER_ROWS - 1; i++, row++)
    {
        if (pv_box[i] < 0)
        {
            gfx_text(s, 20, PAPER_TEXT(row), pv_text[i], C_INK_SOFT);
            continue;
        }
        // A checklist item: its box, then the text (struck through if done).
        gfx_panel(s, 20, PAPER_Y + row * PAPER_ROW + 1, 10, 10, 1, C_WHITE, C_INK_SOFT);
        if (pv_box[i])
            gfx_stamp_check(s, 25, PAPER_TEXT(row) + 3, 5, C_OK);
        int end = gfx_text(s, 35, PAPER_TEXT(row), pv_text[i], C_INK_SOFT);
        if (pv_box[i])
            gfx_hline(s, 35, PAPER_TEXT(row) + 4, end - 35, C_INK_SOFT);
    }
    if (pv_ok && pv_n == 0 && pv_wait == 0)
        gfx_text(s, 20, PAPER_TEXT(row + 1),
                 n->checklist ? "(no items)" : n->bills ? "(no bills)" : "(title only)",
                 C_INK_SOFT);
    if (n->bills && pv_ok && pv_wait == 0)
    {
        // The total under a double rule, like the foot of a ledger.
        int ty = PAPER_TEXT(PAPER_ROWS - 2);
        gfx_hline(s, 20, ty - 4, SCREEN_W - 28, C_INK);
        gfx_hline(s, 20, ty - 2, SCREEN_W - 28, C_INK);
        gfx_text(s, 20, ty, pv_total, C_INK);
    }

    static const char *const KEYS[] = { "A", "Open", "X", "New", "Y", "Menu", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

static void list_row(Surface *s, int i, int y, bool selected)
{
    (void)selected;
    gfx_icon(s, 5, y + 1,
             idx.n[i].sketch      ? ICON_SKETCH
             : idx.n[i].checklist ? ICON_TODO
             : idx.n[i].bills     ? ICON_BILL
                                  : ICON_PAGE,
             1);
    // At a glance, at the right: "2/5" checked, or the bills' total.
    const NoteInfo *n = &idx.n[i];
    char glance[24] = "";
    uint8_t ink = C_INK_SOFT;
    if (n->checklist && n->items > 0)
    {
        snprintf(glance, sizeof(glance), "%d/%d", n->done, n->items);
        if (n->done == n->items)
            ink = C_OK;   // all checked
    }
    else if (n->bills)
    {
        money_format(n->total, glance, sizeof(glance));
    }
    int right = SCREEN_W - 8;
    if (glance[0])
    {
        right -= (int)strlen(glance) * FONT_W;
        gfx_text(s, right, y + 6, glance, ink);
        right -= 8;
    }
    gfx_text_n(s, BOX_W + 6, y + 6, n->title, (right - BOX_W - 6) / FONT_W, C_INK);
}

static void list_draw_bottom(Surface *s)
{
    list.count = idx.count;
    list_draw(&list, s, list_row);
    if (idx.count == 0)
    {
        gfx_text_center(s, SCREEN_W / 2, 70, "Notebook empty.", C_INK_SOFT);
        gfx_text_center(s, SCREEN_W / 2, 86, "Tap Add.", C_INK_SOFT);
    }
    bar.n = 2;
    bar.item[0] = (BarItem){ "Open", ICON_PAGE, SEC_PLAIN, idx.count > 0 };
    bar.item[1] = (BarItem){ "Add", ICON_PLUS, SEC_NOTES, true };
    bar_draw(s, &bar);
}

static bool file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

// The lowest number that no note or sketch uses and that holds no deleted
// note's or sketch's .BAK copy (saving there would overwrite that copy).
static int free_note_id(void)
{
    for (int id = note_next_id(&idx); id > 0 && id <= 9999; id++)
    {
        bool used = false;
        for (int i = 0; i < idx.count && !used; i++)
            used = idx.n[i].id == id;
        // A backup of any of the four kinds of entry takes the number.
        static void (*const PATH[4])(const char *, int, char *, size_t) = {
            note_path, sketch_path, checklist_path, bills_path,
        };
        for (int k = 0; k < 4 && !used; k++)
        {
            char p[PDA_PATH_MAX], bak[PDA_PATH_MAX];
            PATH[k](app_notes_dir(), id, p, sizeof(p));
            text_sibling(p, ".BAK", bak, sizeof(bak));
            used = file_exists(bak);
        }
        if (!used)
            return id;
    }
    return -1;
}

static void open_entry(int i)
{
    if (i < 0 || i >= idx.count)
        return;
    if (idx.n[i].sketch)
        sketch_open(idx.n[i].id, idx.n[i].title);
    else
        open_note(idx.n[i].id, kind_of(&idx.n[i]));
}

static void pick_id(int id)
{
    for (int i = 0; i < idx.count; i++)
        if (idx.n[i].id == id)
            list_show(&list, i);
}

// New sketch: the typed title goes into Sxxxx.TXT, the drawing into
// Sxxxx.BMP when the sketch closes.
static void new_sketch_done(bool ok, const char *text)
{
    if (!ok)
        return;
    int id = free_note_id();
    if (id < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Notebook full.");
        return;
    }
    char bmp[PDA_PATH_MAX], txt[PDA_PATH_MAX], title[NOTE_TITLE_MAX + 1];
    snprintf(title, sizeof(title), "%s", text[0] ? text : "Sketch");
    sketch_path(app_notes_dir(), id, bmp, sizeof(bmp));
    text_sibling(bmp, ".TXT", txt, sizeof(txt));
    char line[NOTE_TITLE_MAX + 2];
    snprintf(line, sizeof(line), "%s\n", title);
    if (!text_save(txt, line, strlen(line)))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Save failed: S%04d.TXT", id);
        return;
    }
    index_stale = true;   // a new sketch joins the list
    sketch_open(id, title);
}

static void view_add(void);

// A new note, checklist or bills: the typed title is its first line.
static void new_doc(const char *text, int kind)
{
    int id = free_note_id();
    if (id < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Notebook full.");
        return;
    }
    note_doc_free(&doc);
    note_doc_init(&doc);
    static const char *const UNTITLED[3] = { "Untitled", "List", "Bills" };
    note_doc_insert(&doc, 0, text[0] ? text : UNTITLED[kind]);
    lines_mode(kind);
    open_id = id;
    if (!save_doc())
        return;
    reload_index();
    pick_id(id);
    lines.sel = lines.scroll = 0;
    line_scroll = 0;
    sfx_play(SFX_SAVED);
    ui_push(&SCR_NOTE_VIEW);
    if (kind != DOC_NOTE)
        view_add();   // straight on to the first item or bill
}

static void new_done(bool ok, const char *text)
{
    if (ok)
        new_doc(text, DOC_NOTE);
}

static void new_list_done(bool ok, const char *text)
{
    if (ok)
        new_doc(text, DOC_LIST);
}

static void new_bills_done(bool ok, const char *text)
{
    if (ok)
        new_doc(text, DOC_BILLS);
}

static void new_chosen(int i)
{
    if (i == 0)
        editor_open("NEW NOTE", "Title?", "", NOTE_TITLE_MAX, new_done);
    else if (i == 1)
        editor_open("NEW CHECKLIST", "Title?", "", NOTE_TITLE_MAX, new_list_done);
    else if (i == 2)
        editor_open("NEW BILLS", "Title? (November)", "", NOTE_TITLE_MAX,
                    new_bills_done);
    else
        editor_open("NEW SKETCH", "Title?", "", NOTE_TITLE_MAX, new_sketch_done);
}

static void list_add(void)
{
    if (!app_storage_ok())
    {
        sfx_play(SFX_ERROR);
        ui_toast("No SD card. Cannot save.");
        return;
    }
    static const SheetItem items[4] = {
        { "Note", ICON_PAGE, SEC_PLAIN },
        { "Checklist", ICON_TODO, SEC_PLAIN },
        { "Bills", ICON_BILL, SEC_PLAIN },
        { "Sketch", ICON_SKETCH, SEC_PLAIN },
    };
    ui_sheet("New", items, 4, new_chosen);
}

// Undo after deleting a whole entry: the file went to .BAK, so it comes
// back by renaming it (and, for a sketch, writing its title again).
static struct {
    int id;
    bool sketch;
    char path[PDA_PATH_MAX], bak[PDA_PATH_MAX];
    char title[NOTE_TITLE_MAX + 1];
} undo_entry;

static void entry_undo(void)
{
    remove(undo_entry.path);   // nothing should be there; rename needs it gone
    if (rename(undo_entry.bak, undo_entry.path) != 0)
    {
        ui_toast("Undo failed.");
        return;
    }
    if (undo_entry.sketch)
    {
        char txt[PDA_PATH_MAX], line[NOTE_TITLE_MAX + 2];
        text_sibling(undo_entry.path, ".TXT", txt, sizeof(txt));
        snprintf(line, sizeof(line), "%s\n", undo_entry.title);
        text_save(txt, line, strlen(line));
    }
    reload_index();
    pick_id(undo_entry.id);
    preview_check();
}

static void delete_yes(void)
{
    if (list.sel >= idx.count)
        return;
    const NoteInfo *n = &idx.n[list.sel];
    char p[PDA_PATH_MAX], bak[PDA_PATH_MAX];
    if (n->sketch)
    {
        // The title file goes; the drawing is kept as Sxxxx.BAK.
        char txt[PDA_PATH_MAX];
        sketch_path(app_notes_dir(), n->id, p, sizeof(p));
        text_sibling(p, ".TXT", txt, sizeof(txt));
        remove(txt);
    }
    else
    {
        doc_path(n->id, kind_of(n), p, sizeof(p));
    }
    text_sibling(p, ".BAK", bak, sizeof(bak));
    remove(bak);
    // Keep one copy as .BAK so a mistaken delete can be undone on a PC.
    bool kept = rename(p, bak) == 0;
    if (!kept)
        remove(p);
    undo_entry.id = n->id;
    undo_entry.sketch = n->sketch;
    snprintf(undo_entry.path, sizeof(undo_entry.path), "%s", p);
    snprintf(undo_entry.bak, sizeof(undo_entry.bak), "%s", bak);
    snprintf(undo_entry.title, sizeof(undo_entry.title), "%s", n->title);
    reload_index();
    sfx_play(SFX_DELETE);
    ui_toast("Deleted. Backup kept.");
    if (kept)
        ui_undo_offer(entry_undo);
    ui_dirty();
}

static void list_delete(void)
{
    if (idx.count == 0)
        return;
    const NoteInfo *n = &idx.n[list.sel];
    ui_confirm(n->sketch      ? "Delete sketch?"
               : n->checklist ? "Delete checklist?"
               : n->bills     ? "Delete bills?"
                              : "Delete note?",
               n->title, "Delete", delete_yes);
}

static bool list_can_delete(void)
{
    return idx.count > 0;
}

// A copy of the picked entry under a new number, titled "<title> (copy)".
// A copied checklist starts with every item unchecked: the same shopping
// list again, or next month's bills to fill in.
static void duplicate_entry(void)
{
    if (list.sel >= idx.count)
        return;
    const NoteInfo *n = &idx.n[list.sel];
    int id = free_note_id();
    if (id < 0)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Notebook full.");
        return;
    }
    char title[NOTE_TITLE_MAX + 1];
    snprintf(title, sizeof(title), "%.33s (copy)", n->title);
    char src[PDA_PATH_MAX], dst[PDA_PATH_MAX];
    bool ok = false;
    if (n->sketch)
    {
        sketch_path(app_notes_dir(), n->id, src, sizeof(src));
        sketch_path(app_notes_dir(), id, dst, sizeof(dst));
        size_t len = 0;
        char *bmp = text_read_file(src, 64 * 1024, &len);
        if (bmp != NULL)
        {
            char txt[PDA_PATH_MAX], line[NOTE_TITLE_MAX + 2];
            text_sibling(dst, ".TXT", txt, sizeof(txt));
            snprintf(line, sizeof(line), "%s\n", title);
            ok = text_save(txt, line, strlen(line)) && text_save(dst, bmp, len);
            free(bmp);
        }
    }
    else
    {
        int kind = kind_of(n);
        doc_path(n->id, kind, src, sizeof(src));
        doc_path(id, kind, dst, sizeof(dst));
        char *text = NULL;
        size_t len = 0;
        if (text_load(src, NOTE_MAX_BYTES, &text, &len) != LOAD_ERROR)
        {
            NoteDoc d;
            note_doc_init(&d);
            if (text != NULL)
                note_doc_from_text(&d, text);
            free(text);
            if (d.count == 0)
                note_doc_insert(&d, 0, title);
            else
                note_doc_set(&d, 0, title);
            for (int i = 1; kind == DOC_LIST && i < d.count; i++)
            {
                bool done;
                char line[NOTE_LINE_MAX + 1];
                checklist_line(false, checklist_item(d.lines[i], &done), line, sizeof(line));
                note_doc_set(&d, i, line);
            }
            TextBuf b;
            textbuf_init(&b);
            ok = note_doc_to_text(&d, &b) && text_save(dst, b.data ? b.data : "", b.len);
            textbuf_free(&b);
            note_doc_free(&d);
        }
    }
    if (!ok)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Copy failed.");
        return;
    }
    reload_index();
    pick_id(id);
    preview_check();
    sfx_play(SFX_SAVED);
    ui_toast("Copied: %s", title);
    ui_dirty();
}

static void list_more_done(int i)
{
    if (i == 0)
        open_entry(list.sel);
    else if (i == 1)
        duplicate_entry();
    else
        list_delete();
}

static void list_more(void)
{
    if (idx.count == 0)
    {
        ui_toast("Nothing here. Tap Add.");
        return;
    }
    static const SheetItem items[3] = {
        { "Open", ICON_PAGE, SEC_PLAIN },
        { "Duplicate", ICON_PLUS, SEC_PLAIN },
        { "Delete", ICON_TRASH, SEC_ALERT },
    };
    ui_sheet("Notes", items, 3, list_more_done);
}

static void notes_update(const Input *in)
{
    if (pv_wait > 0 && --pv_wait == 0)
    {
        preview_load();
        ui_dirty_top();
    }
    list.count = idx.count;
    int b = bar_update(&bar, in);
    if (b == 0)
        open_entry(list.sel);
    else if (b == 1)
        list_add();
    if (b >= 0)
        return;
    ListEvent ev = list_update(&list, in);
    if (ev.kind == LE_BOX || ev.kind == LE_OPEN)
        open_entry(ev.index);
    else
        preview_check();
}

static void list_enter(void)
{
    if (list.height == 0)
    {
        list_init(&list, UI_HEAD_H, UI_BAR_Y - UI_HEAD_H, BOX_W);
        list_init(&lines, UI_HEAD_H, UI_BAR_Y - UI_HEAD_H, 0);
    }
    if (index_stale)
        reload_index();
    pv_id = -1;   // the picked note or sketch may have changed
    list.count = idx.count;
    list_fix(&list);
    preview_check();
}

const Screen SCR_NOTES = {
    .enter = list_enter,
    .update = notes_update,
    .draw_top = list_draw_top,
    .draw_bottom = list_draw_bottom,
    .title = "Notes",
    .icon = ICON_NOTES,
    .sec = SEC_NOTES,
    .on_add = list_add,
    .on_delete = list_delete,
    .can_delete = list_can_delete,
    .on_more = list_more,
};

// ---------------------------------------------------------------- note

// The open note line by line, a checklist, or bills. Line 0 is the title;
// in a checklist every other line is an item with a box at the left, in
// bills a bill with its amount at the right and the total below the list.

#define WRAP 29
#define ITEM_WRAP 26   // a checklist item's text, after its box
#define VIEW_ROWS 13
#define BILL_ROWS 11   // bills on the top screen: two rows kept for the total
#define AMOUNT_X 150   // bills: a tap right of this edits the amount

#define WRAP_ROWS 12   // most display rows one line can take

// Display rows each line takes when word-wrapped, measured once per change
// rather than on every redraw.
static uint8_t row_count[NOTE_MAX_LINES];

static bool is_item(int i)
{
    return open_list && i > 0;
}

static bool is_bill(int i)
{
    return open_bills && i > 0;
}

static int view_rows(void)
{
    return open_bills ? BILL_ROWS : VIEW_ROWS;
}

// Wraps line i for the page. An empty line still takes one row; a bill is
// one row (its name cut to fit beside the amount).
static int wrap_line(int i, char out[][TEXT_WRAP_W + 1])
{
    int n;
    if (is_bill(i))
    {
        out[0][0] = '\0';
        return 1;
    }
    if (is_item(i))
    {
        bool done;
        n = text_wrap(checklist_item(doc.lines[i], &done), ITEM_WRAP, out, WRAP_ROWS);
    }
    else
    {
        n = text_wrap(doc.lines[i], WRAP, out, WRAP_ROWS);
    }
    if (n == 0)
    {
        out[0][0] = 0;
        n = 1;
    }
    return n;
}

static int line_rows(int i)
{
    if (rows_stale)
    {
        char tmp[WRAP_ROWS][TEXT_WRAP_W + 1];
        for (int k = 0; k < doc.count; k++)
            row_count[k] = (uint8_t)wrap_line(k, tmp);
        rows_stale = false;
    }
    return row_count[i];
}

static int first_row_of(int line)
{
    int r = 0;
    for (int i = 0; i < line; i++)
        r += line_rows(i);
    return r;
}

// Items checked off, for the header.
static int done_count(void)
{
    int n = 0;
    for (int i = 1; i < doc.count; i++)
    {
        bool done;
        checklist_item(doc.lines[i], &done);
        n += done;
    }
    return n;
}

// Keeps the picked line on the top screen's page.
static void view_clamp(void)
{
    lines.count = doc.count;
    list_fix(&lines);
    if (doc.count == 0)
    {
        line_scroll = 0;
        return;
    }
    int top = first_row_of(lines.sel);
    int bottom = top + line_rows(lines.sel);
    if (top < line_scroll)
        line_scroll = top;
    if (bottom > line_scroll + view_rows())
        line_scroll = bottom - view_rows();
    if (line_scroll < 0)
        line_scroll = 0;
}

static void view_draw_top(Surface *s)
{
    char right[24];
    if (open_list)
        snprintf(right, sizeof(right), "%d of %d done", done_count(),
                 doc.count > 0 ? doc.count - 1 : 0);
    else if (open_bills)
        snprintf(right, sizeof(right), "%d bill%s", doc.count > 0 ? doc.count - 1 : 0,
                 doc.count == 2 ? "" : "s");
    else
        snprintf(right, sizeof(right), "line %d of %d", doc.count ? lines.sel + 1 : 0, doc.count);
    gfx_header(s, open_list ? "List" : open_bills ? "Bills" : "Note", right);
    gfx_paper(s, PAPER_Y);
    view_clamp();

    int row = 0;  // absolute display row
    for (int i = 0; i < doc.count; i++)
    {
        int n = line_rows(i);
        if (row + n <= line_scroll)
        {
            row += n;   // above the page
            continue;
        }
        char part[WRAP_ROWS][TEXT_WRAP_W + 1];
        wrap_line(i, part);
        bool done = false;
        if (is_item(i))
            checklist_item(doc.lines[i], &done);
        for (int k = 0; k < n; k++, row++)
        {
            int vr = row - line_scroll;
            if (vr < 0)
                continue;
            if (vr >= view_rows())
                goto done;
            int y = PAPER_Y + vr * PAPER_ROW;
            if (i == lines.sel)
                gfx_fill(s, 17, y, SCREEN_W - 17, PAPER_ROW - 1, C_SEL);
            if (is_bill(i))
            {
                char name[NOTE_LINE_MAX + 1], text[TEXT_WRAP_W + 1];
                long c;
                bill_item(doc.lines[i], name, sizeof(name), &c);
                bill_row_text(name, c, 29, text, sizeof(text));
                gfx_text(s, 20, PAPER_TEXT(vr), text, c < 0 ? C_OK : C_INK);
                continue;
            }
            if (!is_item(i))
            {
                gfx_text(s, 20, PAPER_TEXT(vr), part[k], i == 0 ? C_ACCENT : C_INK);
                continue;
            }
            if (k == 0)
            {
                gfx_panel(s, 20, y + 1, 10, 10, 1, C_WHITE, C_INK_SOFT);
                if (done)
                    gfx_stamp_check(s, 25, PAPER_TEXT(vr) + 3, 5, C_OK);
            }
            int end = gfx_text(s, 36, PAPER_TEXT(vr), part[k], done ? C_INK_SOFT : C_INK);
            if (done)
                gfx_hline(s, 36, PAPER_TEXT(vr) + 4, end - 36, C_INK_SOFT);
        }
    }
done:;
    if (open_bills)
    {
        // The total under a double rule, like the foot of a ledger.
        char total[TEXT_WRAP_W + 1];
        bill_row_text("TOTAL", bills_total(&doc), 29, total, sizeof(total));
        int ty = PAPER_TEXT(PAPER_ROWS - 2);
        gfx_hline(s, 20, ty - 4, SCREEN_W - 28, C_INK);
        gfx_hline(s, 20, ty - 2, SCREEN_W - 28, C_INK);
        gfx_text(s, 20, ty, total, C_INK);
    }
    static const char *const KEYS[] = { "A", "Edit", "X", "Add line", "Y", "Menu", NULL };
    static const char *const LIST_KEYS[] = { "A", "Check", "X", "Add item", "Y", "Menu", NULL };
    static const char *const BILL_KEYS[] = { "A", "Edit", "X", "Add bill", "Y", "Menu", NULL };
    legend_draw(s, PAPER_TEXT(13), open_list ? LIST_KEYS : open_bills ? BILL_KEYS : KEYS);
}

static void line_row(Surface *s, int i, int y, bool selected)
{
    (void)selected;
    const char *line = doc.lines[i];
    if (is_bill(i))
    {
        char name[NOTE_LINE_MAX + 1], amount[24];
        long c;
        bill_item(line, name, sizeof(name), &c);
        money_format(c, amount, sizeof(amount));
        int ax = SCREEN_W - 8 - (int)strlen(amount) * FONT_W;
        gfx_text(s, ax, y + 6, amount, c < 0 ? C_OK : C_INK);
        gfx_text_n(s, 10, y + 6, name, (ax - 8 - 10) / FONT_W, C_INK);
        return;
    }
    if (is_item(i))
    {
        bool done;
        const char *text = checklist_item(line, &done);
        gfx_check_box(s, 6, y + 2);
        if (done)
            gfx_stamp_check(s, 13, y + 9, 7, C_OK);
        int x = LINE_BOX_W + 6;
        int end = gfx_text_n(s, x, y + 6, text, (SCREEN_W - 8 - x) / FONT_W,
                             done ? C_INK_SOFT : C_INK);
        if (done)
            gfx_hline(s, x, y + 10, end - x, C_INK_SOFT);
        return;
    }
    int x = open_list ? LINE_BOX_W + 6 : 10;
    if (line[0] == '\0')
        gfx_text(s, x, y + 6, "(empty)", C_SHADOW);
    else
        gfx_text_n(s, x, y + 6, line, (SCREEN_W - 8 - x) / FONT_W, i == 0 ? C_ACCENT : C_INK);
}

// Bills: the total on its own row between the list and the action bar.
static void total_draw(Surface *s)
{
    int y = UI_BAR_Y - TOTAL_H;
    gfx_fill(s, 0, y, SCREEN_W, TOTAL_H, C_PAPER_SH);
    gfx_hline(s, 0, y, SCREEN_W, C_INK);
    gfx_hline(s, 0, y + 2, SCREEN_W, C_INK);
    char amount[24];
    long long total = bills_total(&doc);
    money_format(total, amount, sizeof(amount));
    gfx_text(s, 10, y + 8, "Total", C_INK_SOFT);
    gfx_text(s, SCREEN_W - 8 - (int)strlen(amount) * FONT_W, y + 8, amount,
             total < 0 ? C_OK : C_INK);
}

static void view_draw_bottom(Surface *s)
{
    lines.count = doc.count;
    list_draw(&lines, s, line_row);
    if (open_bills)
        total_draw(s);
    if ((open_list || open_bills) && doc.count <= 1)
        gfx_text_center(s, SCREEN_W / 2, 86, "Tap Add.", C_INK_SOFT);
    // Add is the bar's last key; Delete is the header's trash.
    if (open_list)
    {
        bool item = is_item(lines.sel) && lines.sel < doc.count;
        bool done = false;
        if (item)
            checklist_item(doc.lines[lines.sel], &done);
        line_bar.n = 3;
        line_bar.item[0] = (BarItem){ done ? "Uncheck" : "Check", ICON_CHECK, SEC_PLAIN, item };
        line_bar.item[1] = (BarItem){ "Edit", ICON_EDIT, SEC_PLAIN, doc.count > 0 };
        line_bar.item[2] = (BarItem){ "Add", ICON_PLUS, SEC_NOTES, true };
    }
    else if (open_bills)
    {
        bool bill = is_bill(lines.sel) && lines.sel < doc.count;
        line_bar.n = 3;
        line_bar.item[0] = (BarItem){ "Edit", ICON_EDIT, SEC_PLAIN, doc.count > 0 };
        line_bar.item[1] = (BarItem){ "Amount", ICON_BILL, SEC_PLAIN, bill };
        line_bar.item[2] = (BarItem){ "Add", ICON_PLUS, SEC_NOTES, true };
    }
    else
    {
        line_bar.n = 2;
        line_bar.item[0] = (BarItem){ "Edit line", ICON_EDIT, SEC_PLAIN, doc.count > 0 };
        line_bar.item[1] = (BarItem){ "Add line", ICON_PLUS, SEC_NOTES, true };
    }
    bar_draw(s, &line_bar);
}

// Checks or unchecks item i.
static void toggle_item(int i)
{
    if (!is_item(i) || i >= doc.count)
        return;
    bool done;
    const char *text = checklist_item(doc.lines[i], &done);
    char line[NOTE_LINE_MAX + 1];
    checklist_line(!done, text, line, sizeof(line));
    if (!note_doc_set(&doc, i, line))
    {
        sfx_play(SFX_ERROR);
        ui_toast("List full (16 KB).");
        return;
    }
    sfx_play(done ? SFX_UNCHECK : SFX_STAMP);
    save_doc();
}

// ---- bills: the name and the amount are edited apart

static int bill_at = -1;   // the bill whose amount is being typed

static bool set_bill(int i, const char *name, long cents)
{
    char line[NOTE_LINE_MAX + 1];
    bill_line(name, cents, line, sizeof(line));
    if (!note_doc_set(&doc, i, line))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Bills full (16 KB).");
        return false;
    }
    return true;
}

static void amount_done(bool ok, const char *text)
{
    if (!ok || !is_bill(bill_at) || bill_at >= doc.count)
        return;
    long cents = 0;
    if (text[0] != '\0' && !money_parse(text, &cents))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Not a number. Try 84.50");
        return;
    }
    char name[NOTE_LINE_MAX + 1];
    long old;
    bill_item(doc.lines[bill_at], name, sizeof(name), &old);
    if (set_bill(bill_at, name, cents))
    {
        sfx_play(SFX_SAVED);
        save_doc();
    }
}

static void edit_amount(int i)
{
    if (!is_bill(i) || i >= doc.count)
        return;
    char name[NOTE_LINE_MAX + 1], init[24] = "";
    long c;
    bill_item(doc.lines[i], name, sizeof(name), &c);
    if (c != 0)
    {
        long v = c < 0 ? -c : c;
        snprintf(init, sizeof(init), "%s%ld.%02ld", c < 0 ? "-" : "", v / 100, v % 100);
    }
    bill_at = i;
    char prompt[48];
    snprintf(prompt, sizeof(prompt), "Amount for %.20s?", name);
    editor_open_number("AMOUNT", prompt, init, 14, amount_done);
}

static void edit_line_done(bool ok, const char *text)
{
    if (!ok || lines.sel >= doc.count)
        return;
    char line[NOTE_LINE_MAX + 1];
    if (is_bill(lines.sel))
    {
        if (text[0] == '\0')
        {
            sfx_play(SFX_ERROR);
            ui_toast("No text. Use the trash.");
            return;
        }
        // "Phone 45.99" sets both; a name alone keeps the amount.
        char name[NOTE_LINE_MAX + 1], old_name[NOTE_LINE_MAX + 1];
        long cents;
        bill_item(doc.lines[lines.sel], old_name, sizeof(old_name), &cents);
        if (!bill_split(text, name, sizeof(name), &cents))
            snprintf(name, sizeof(name), "%s", text);
        if (set_bill(lines.sel, name, cents))
        {
            sfx_play(SFX_SAVED);
            save_doc();
        }
        return;
    }
    if (is_item(lines.sel))
    {
        if (text[0] == '\0')
        {
            sfx_play(SFX_ERROR);
            ui_toast("No text. Use the trash.");
            return;
        }
        bool done;
        checklist_item(doc.lines[lines.sel], &done);   // keeps its check
        checklist_line(done, text, line, sizeof(line));
        text = line;
    }
    if (!note_doc_set(&doc, lines.sel, text))
    {
        sfx_play(SFX_ERROR);
        ui_toast("Note full (16 KB).");
        return;
    }
    sfx_play(SFX_SAVED);
    save_doc();
}

static void add_line_done(bool ok, const char *text)
{
    if (!ok)
        return;
    char line[NOTE_LINE_MAX + 1];
    bool ask_amount = false;
    if (open_list || open_bills)
    {
        if (text[0] == '\0')
            return;   // nothing typed: nothing to add
    }
    if (open_list)
    {
        checklist_line(false, text, line, sizeof(line));
        text = line;
    }
    else if (open_bills)
    {
        // "Electric 84.50" is all it needs; a name alone asks the amount next.
        char name[NOTE_LINE_MAX + 1];
        long cents = 0;
        if (!bill_split(text, name, sizeof(name), &cents))
        {
            snprintf(name, sizeof(name), "%s", text);
            ask_amount = true;
        }
        bill_line(name, cents, line, sizeof(line));
        text = line;
    }
    int at = doc.count ? lines.sel + 1 : 0;
    if (!note_doc_insert(&doc, at, text))
    {
        sfx_play(SFX_ERROR);
        ui_toast(open_list    ? "List full (16 KB)."
                 : open_bills ? "Bills full (16 KB)."
                              : "Note full (16 KB).");
        return;
    }
    lines.count = doc.count;
    list_show(&lines, at);
    sfx_play(SFX_SAVED);
    save_doc();
    if (ask_amount)
        edit_amount(at);
}

static void view_edit(void)
{
    if (doc.count == 0)
        return;
    if (is_bill(lines.sel))
    {
        char name[NOTE_LINE_MAX + 1];
        long c;
        bill_item(doc.lines[lines.sel], name, sizeof(name), &c);
        editor_open("EDIT BILL", "Item text?", name, NOTE_LINE_MAX - 16, edit_line_done);
        return;
    }
    if (is_item(lines.sel))
    {
        bool done;
        editor_open("EDIT ITEM", "Edit the item.", checklist_item(doc.lines[lines.sel], &done),
                    NOTE_LINE_MAX - 4, edit_line_done);
        return;
    }
    editor_open("EDIT LINE", lines.sel == 0 ? "The title" : "Edit the line.",
                doc.lines[lines.sel], NOTE_LINE_MAX, edit_line_done);
}

static void view_add(void)
{
    if (open_list)
        editor_open("NEW ITEM", "It goes below.", "", NOTE_LINE_MAX - 4,
                    add_line_done);
    else if (open_bills)
        editor_open("NEW BILL", "Name, amount: Rent 1200", "", NOTE_LINE_MAX - 16,
                    add_line_done);
    else
        editor_open("NEW LINE", "It goes below.", "", NOTE_LINE_MAX,
                    add_line_done);
}

// Undo inside a note: its text as it was before the delete. The offer goes
// when the note closes, so it never lands in another one.
static char *undo_text = NULL;
static int undo_line = 0;

static void doc_snapshot(void)
{
    TextBuf b;
    textbuf_init(&b);
    free(undo_text);
    undo_text = NULL;
    if (note_doc_to_text(&doc, &b))
        undo_text = b.data;   // keeps the buffer
    else
        textbuf_free(&b);
    undo_line = lines.sel;
}

static void doc_undo(void)
{
    if (undo_text == NULL)
        return;
    note_doc_from_text(&doc, undo_text);
    free(undo_text);
    undo_text = NULL;
    save_doc();
    lines.count = doc.count;
    list_show(&lines, undo_line);
}

static void delete_line_yes(void)
{
    if (lines.sel <= 0 || lines.sel >= doc.count)
        return;
    doc_snapshot();
    note_doc_delete(&doc, lines.sel);
    sfx_play(SFX_DELETE);
    save_doc();
    if (undo_text != NULL)
        ui_undo_offer(doc_undo);
}

// The header's trash: everything but the title can go.
static bool view_can_delete(void)
{
    return lines.sel > 0 && lines.sel < doc.count;
}

static void view_delete(void)
{
    if (doc.count == 0)
        return;
    if (lines.sel == 0)
    {
        ui_toast("Title stays. Edit it.");
        return;
    }
    bool done;
    char name[NOTE_LINE_MAX + 1];
    long c;
    const char *text = doc.lines[lines.sel];
    if (open_list)
        text = checklist_item(doc.lines[lines.sel], &done);
    else if (open_bills)
    {
        bill_item(doc.lines[lines.sel], name, sizeof(name), &c);
        text = name;
    }
    ui_confirm(open_list ? "Delete item?" : open_bills ? "Delete bill?" : "Delete line?",
               text, "Delete", delete_line_yes);
}

// Checklist menu: removes the checked items. The list as it was stays in
// Lxxxx.BAK, as with every save.
static void clear_checked_yes(void)
{
    doc_snapshot();
    int removed = 0;
    for (int i = doc.count - 1; i > 0; i--)
    {
        bool done;
        checklist_item(doc.lines[i], &done);
        if (done)
        {
            note_doc_delete(&doc, i);
            removed++;
        }
    }
    lines.sel = lines.scroll = 0;
    sfx_play(SFX_DELETE);
    save_doc();
    ui_toast("Removed %d item%s.", removed, removed == 1 ? "" : "s");
    if (undo_text != NULL)
        ui_undo_offer(doc_undo);
}

static void uncheck_all(void)
{
    for (int i = 1; i < doc.count; i++)
    {
        bool done;
        const char *text = checklist_item(doc.lines[i], &done);
        if (!done)
            continue;
        char line[NOTE_LINE_MAX + 1];
        checklist_line(false, text, line, sizeof(line));
        note_doc_set(&doc, i, line);   // same length: cannot fail on size
    }
    sfx_play(SFX_UNCHECK);
    save_doc();
    ui_toast("All unchecked.");
}

static void list_more_action(int i)
{
    switch (i)
    {
        case 0: toggle_item(lines.sel); break;
        case 1: view_edit(); break;
        case 2: view_add(); break;
        case 3: view_delete(); break;
        case 4:
        {
            int n = done_count();
            if (n == 0)
            {
                ui_toast("None checked.");
                return;
            }
            static char q[40];
            snprintf(q, sizeof(q), "Remove %d item%s?", n, n == 1 ? "" : "s");
            ui_confirm(q, "Backup kept.", "Remove", clear_checked_yes);
            break;
        }
        case 5: uncheck_all(); break;
    }
}

static void bills_more_action(int i)
{
    switch (i)
    {
        case 0: view_edit(); break;
        case 1: edit_amount(lines.sel); break;
        case 2: view_add(); break;
        case 3: view_delete(); break;
    }
}

static void view_more_done(int i)
{
    switch (i)
    {
        case 0: view_edit(); break;
        case 1: view_add(); break;
        case 2: view_delete(); break;
    }
}

static void view_more(void)
{
    if (open_list)
    {
        static const SheetItem items[6] = {
            { "Check", ICON_CHECK, SEC_PLAIN },
            { "Edit", ICON_EDIT, SEC_PLAIN },
            { "Add item", ICON_PLUS, SEC_PLAIN },
            { "Delete", ICON_TRASH, SEC_ALERT },
            { "Clear checked", ICON_CHECK, SEC_PLAIN },
            { "Uncheck all", ICON_UNDO, SEC_PLAIN },
        };
        ui_sheet("Checklist", items, 6, list_more_action);
        return;
    }
    if (open_bills)
    {
        static const SheetItem items[4] = {
            { "Edit name", ICON_EDIT, SEC_PLAIN },
            { "Edit amount", ICON_BILL, SEC_PLAIN },
            { "Add bill", ICON_PLUS, SEC_PLAIN },
            { "Delete", ICON_TRASH, SEC_ALERT },
        };
        ui_sheet("Bills", items, 4, bills_more_action);
        return;
    }
    static const SheetItem items[3] = {
        { "Edit line", ICON_EDIT, SEC_PLAIN },
        { "Add line", ICON_PLUS, SEC_PLAIN },
        { "Delete line", ICON_TRASH, SEC_ALERT },
    };
    ui_sheet("Note", items, 3, view_more_done);
}

static void view_update(const Input *in)
{
    lines.count = doc.count;
    int b = bar_update(&line_bar, in);
    if (b == line_bar.n - 1)
        view_add();                     // Add is the last key everywhere
    else if (open_list && b == 0)
        toggle_item(lines.sel);
    else if (open_bills && b == 1)
        edit_amount(lines.sel);
    else if (b >= 0)
        view_edit();                    // Edit / Edit line
    if (b >= 0)
        return;
    ListEvent ev = list_update(&lines, in);
    if (ev.kind == LE_BOX && is_item(ev.index))
        toggle_item(ev.index);          // the box, or A on an item
    else if (ev.kind == LE_OPEN && is_bill(ev.index) && in->touch_up && in->tx >= AMOUNT_X)
        edit_amount(ev.index);          // the amount of the picked bill
    else if (ev.kind == LE_OPEN || ev.kind == LE_BOX)
        view_edit();                    // the picked line again, or A on the title
}

static void view_leave(void)
{
    note_doc_free(&doc);
}

static const char *view_title(void)
{
    static char t[20];
    snprintf(t, sizeof(t), "%.14s",
             doc.count ? doc.lines[0] : open_list ? "List" : open_bills ? "Bills" : "Note");
    return t;
}

const Screen SCR_NOTE_VIEW = {
    .leave = view_leave,
    .update = view_update,
    .draw_top = view_draw_top,
    .draw_bottom = view_draw_bottom,
    .title_fn = view_title,
    .icon = ICON_PAGE,
    .sec = SEC_NOTES,
    .on_add = view_add,
    .on_more = view_more,
    .on_delete = view_delete,
    .can_delete = view_can_delete,
};
