// TODO.TXT: one item per line.
//   [ ] Buy milk
//   [x] Call bank due:2026-11-21
//   [ ] Water plants due:2026-11-23 every:3d
// "every:" repeats a to-do: N (optional, default 1) and d, w, m or y
// (day, week, month, year). Checking a repeating to-do moves its due date
// to the next one after today instead of marking it done.
// Lines starting with '#' and blank lines are ignored. A line without a
// checkbox is read as an open item.
#ifndef STORE_TODO_IO_H
#define STORE_TODO_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "date.h"
#include "textio.h"

#define TODO_MAX 256
#define TODO_TEXT_MAX 120

typedef struct {
    bool done;
    bool has_due;
    Date due;
    char repeat;     // 0 = none, or 'd', 'w', 'm', 'y'
    int every;       // repeat interval, 1..99
    char text[TODO_TEXT_MAX + 1];
} TodoItem;

typedef struct {
    int count;
    TodoItem items[TODO_MAX];
} TodoList;

void todo_clear(TodoList *l);
// Parses text (modified in place). Items past TODO_MAX are dropped.
void todo_parse(TodoList *l, char *text);
// Appends the file contents to b. Returns false on allocation failure.
bool todo_serialize(const TodoList *l, TextBuf *b);

// Returns the new index, or -1 when the list is full or text is empty.
int todo_add(TodoList *l, const char *text);
void todo_remove(TodoList *l, int index);
// Swaps items index and index+dir. Returns the new index of the item.
int todo_move(TodoList *l, int index, int dir);
// Sets text, pulling trailing "due:YYYY-MM-DD" and "every:..." tokens (in
// either order) into the due and repeat fields.
void todo_set_text(TodoItem *it, const char *text);
// The same for an edit of an item that exists: a date or repeat typed in the
// text replaces the item's, and without one the item keeps what it had.
// Returns false, leaving the item as it was, when no text is left.
bool todo_edit_text(TodoItem *it, const char *text);
// Parses "w", "2w", "3d", "week", "monthly"... into a unit and interval.
bool todo_parse_every(const char *s, char *unit, int *every);
// Writes "every 2 weeks", "every day", "never" (for 0) into out.
void todo_repeat_text(char unit, int every, char *out, int size);
// The due date after `due` for a repeat rule.
Date todo_next_due(Date due, char unit, int every);
// Days the item falls on in [first, first + ndays) (ndays <= 32): bit d is
// first + d. Its due date, plus every repeat of it in the window. Items
// without a date give 0.
uint32_t todo_days_mask(const TodoItem *it, Date first, int ndays);
// Stable sort: open items by due date (earliest first), then open items
// without a date, then checked items, each group keeping its order.
void todo_sort_by_due(TodoList *l);
// Checks an item off. A repeating item stays open and moves to its next due
// date after today (counting from today when it has none); returns true
// when it did that.
bool todo_complete(TodoItem *it, Date today);
// Rewrites a trailing "due:<word>" token (see date_parse_relative) in text
// to "due:YYYY-MM-DD". Returns 0 when there is no due token, 1 when it was
// understood, -1 when the word is not a date (text is left unchanged).
int todo_expand_due(char *text, size_t size, Date today);
// Number of items with done == false.
int todo_open_count(const TodoList *l);

#endif
