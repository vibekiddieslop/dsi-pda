// Notes live in <root>/NOTES/Nxxxx.TXT, one plain-text file per note.
// The first line is the title. Each note is capped at NOTE_MAX_BYTES.
// Sketches share the same numbers: Sxxxx.BMP holds the drawing and
// Sxxxx.TXT its title. Checklists are Lxxxx.TXT: the title, then one item
// per line, "[ ] Milk" or "[x] Eggs" (a line without a box is an open item).
// Bills are Bxxxx.TXT: the title, then "Rent|1200.00" lines (what, then the
// amount after the last '|'); the page shows the total.
#ifndef STORE_NOTES_IO_H
#define STORE_NOTES_IO_H

#include <stdbool.h>

#include "textio.h"

#define NOTE_MAX_BYTES (16 * 1024)
#define NOTE_MAX_LINES 1024
#define NOTE_LINE_MAX 200
#define NOTES_MAX 200
#define NOTE_TITLE_MAX 40

typedef struct {
    int id;                          // 1..9999 -> N0001.TXT, S0001.BMP, L0001.TXT
    bool sketch;
    bool checklist;
    bool bills;
    int items, done;                 // checklist: items, and how many are checked
    long long total;                 // bills: the sum, in cents
    char title[NOTE_TITLE_MAX + 1];
} NoteInfo;

typedef struct {
    int count;
    NoteInfo n[NOTES_MAX];
} NoteIndex;

// A note held as an array of malloc'd lines.
typedef struct {
    int count;
    char *lines[NOTE_MAX_LINES];
} NoteDoc;

// Builds "<dir>/N0001.TXT".
void note_path(const char *dir, int id, char *out, size_t size);
// Builds "<dir>/S0001.BMP" (the title file is the same name with .TXT).
void sketch_path(const char *dir, int id, char *out, size_t size);
// Builds "<dir>/L0001.TXT".
void checklist_path(const char *dir, int id, char *out, size_t size);
// Builds "<dir>/B0001.TXT".
void bills_path(const char *dir, int id, char *out, size_t size);
// Lists Nxxxx.TXT notes, Sxxxx.BMP sketches, Lxxxx.TXT checklists and
// Bxxxx.TXT bills in dir, sorted by id, with titles. Returns false if the
// directory cannot be opened.
bool note_index_load(NoteIndex *idx, const char *dir);
// First unused id, or -1 when all 9999 are taken.
int note_next_id(const NoteIndex *idx);

void note_doc_init(NoteDoc *d);
void note_doc_free(NoteDoc *d);
// Splits text into lines (text is not kept). Returns false on allocation failure.
bool note_doc_from_text(NoteDoc *d, const char *text);
bool note_doc_to_text(const NoteDoc *d, TextBuf *b);
size_t note_doc_size(const NoteDoc *d);
// Line operations. Each returns false when a limit would be exceeded.
bool note_doc_insert(NoteDoc *d, int at, const char *line);
bool note_doc_set(NoteDoc *d, int at, const char *line);
void note_doc_delete(NoteDoc *d, int at);

// A checklist line: sets *done and returns the item's text (after "[x] ").
const char *checklist_item(const char *line, bool *done);
// Writes the line for an item: "[ ] text" or "[x] text".
void checklist_line(bool done, const char *text, char *out, size_t size);

// Money, in cents. Reads "12", "12.5", "$1,200.00", "-3.10" (at most
// 9,999,999.99 either way). False for anything else, or nothing.
#define MONEY_MAX 999999999L
bool money_parse(const char *s, long *cents);
// "$1,200.00", "-$3.10".
void money_format(long long cents, char *out, size_t size);
// A bill line: copies what it is to text and sets *cents. A line without
// a readable amount is all text, 0 cents.
void bill_item(const char *line, char *text, size_t text_size, long *cents);
// Writes "Rent|1200.00".
void bill_line(const char *text, long cents, char *out, size_t size);
// "Electric 84.50" -> "Electric" and 8450 cents. False when the last word
// is not an amount or nothing comes before it.
bool bill_split(const char *input, char *text, size_t text_size, long *cents);
// The sum of lines 1.. (line 0 is the title).
long long bills_total(const NoteDoc *d);

#endif
