#ifndef SCREENS_SCREENS_H
#define SCREENS_SCREENS_H

#include <stdbool.h>

#include "store/cal_io.h"
#include "store/date.h"
#include "store/todo_io.h"
#include "ui/ui.h"

extern const Screen SCR_HOME;
extern const Screen SCR_TODO;
extern const Screen SCR_NOTES;
extern const Screen SCR_NOTE_VIEW;
extern const Screen SCR_CALENDAR;
extern const Screen SCR_TIMERS;
extern const Screen SCR_LAUNCH;    // Games (TWiLight Menu++) and [apps]
extern const Screen SCR_AGENDA;
extern const Screen SCR_HABITS;
extern const Screen SCR_SKETCH;
extern const Screen SCR_SETTINGS;
extern const Screen SCR_SHIFTS;     // the saved work shift types (shift.c)
extern const Screen SCR_HWR_TRAIN;  // Settings > Train handwriting (hwr_train.c)

// A habit's month calendar and stats (habit_history.c).
void habit_history_open(int habit_index);

// Selects the Games entry, for START on Home.
void launch_select_games(void);
// The month view shows this day next time it appears (Agenda "Open day").
void calendar_show_day(Date d);
// Opens the sketch with this id (a new blank one when no file exists).
void sketch_open(int id, const char *title);
// Reads a sketch into dst (SKETCH_W x SKETCH_H screen palette entries).
// False when there is no such file or it is not a PDA sketch.
#define SKETCH_W 256
#define SKETCH_H 140
bool sketch_read(int id, uint8_t *dst);

// ---- helpers shared by the screens (common.c)

// Today, by the console's clock.
Date today_date(void);
// Copies typed to-do text to out with a "due:fri" at its end made a date
// ("due:YYYY-MM-DD"). A due word that is not understood stays in the text,
// and a note says so.
void todo_expand_typed(const char *typed, char *out, size_t size);

// Header with "Fri 20 Nov 14:05" on the right.
void draw_clock_header(Surface *s, const char *title);

// Word-wraps text into lines of at most `width` characters (width <=
// TEXT_WRAP_W). Returns the number of lines; text past max_lines is cut and
// the last line then ends in '~'.
#define TEXT_WRAP_W 32
int text_wrap(const char *text, int width, char lines[][TEXT_WRAP_W + 1], int max_lines);

// A title on the page from ruled row `row`: written big (two rows) when it is
// short, else wrapped over up to 5 rows. Returns the row after it.
int title_draw(Surface *s, const char *text, int row, uint8_t ink);
// A to-do in full from the top of the page: its state, text, due date and
// repeat. `right` (may be NULL) goes at the right of the first row. Returns
// the row after it.
int todo_detail_draw(Surface *s, const TodoItem *it, Date today, const char *right);
// Tear-off calendar block, 42x42 px: month, day number, weekday.
void date_block_draw(Surface *s, int x, int y, Date d);
// A clock face of radius r showing `minute` (since midnight); with bells it
// is an alarm clock.
void clock_draw(Surface *s, int cx, int cy, int r, int minute, bool bells);

// ---- calendar entries (cal_edit.c). Each opens the keyboard, a card or a
// dialog as needed and saves. The screen showing the entries registers a
// callback to refresh itself after every change.
void caledit_on_change(void (*cb)(void));
// Asks what to add to this day: event, to-do, birthday or work shift.
void caledit_add(Date day);
void caledit_event_edit(int index);     // the text, or the shift picker
void caledit_event_time(int index);
void caledit_event_repeat(int index);   // next repeat; birthday <-> anniversary
void caledit_event_delete(int index);
// Checks or unchecks a to-do. True when it was stamped done (a repeating
// one moves on instead).
bool caledit_todo_check(int index);
void caledit_todo_edit(int index);
void caledit_todo_date(int index);
void caledit_todo_delete(int index);

// The entries of `days` days from `from` as one list (agenda.c): one day,
// or the 30-day agenda, which starts with the overdue to-dos.
#define AGENDA_DAYS 30
void agenda_open(Date from, int days);

// ---- time picker (picker.c): big +/- keys instead of typing a time.

typedef enum {
    PICK_ALARM,   // a = hour 0..23, b = minute 0..59, plus days and label
    PICK_TIMER,   // a = minutes 0..99, b = seconds 0..59, plus preset keys
} PickMode;

typedef struct {
    int a, b;
    char days[8];       // "MTWTFSS" with '-' for skipped days (alarm)
    char label[41];     // alarm label (ALARM_LABEL_MAX)
} PickValue;

// Called after the picker closed with Save/Start or a timer preset.
typedef void (*PickDone)(const PickValue *v);
void picker_open(PickMode mode, const char *title, const PickValue *init, PickDone done);
// A new timer or alarm: opens in `start` mode with a key in the header to
// switch to the other one. Each keeps its own value and calls its own done.
void picker_open_new(PickMode start, const PickValue *timer_init, const PickValue *alarm_init,
                     PickDone timer_done, PickDone alarm_done);

// ---- due date and repeat picker for to-dos (when.c)

typedef struct {
    bool has_due;
    Date due;
    char repeat;   // 0 or 'd', 'w', 'm', 'y'
    int every;     // 1..4
} WhenValue;

// Called after Save. Back leaves the to-do as it was.
typedef void (*WhenDone)(const WhenValue *v);
void when_open(const char *todo_text, const WhenValue *init, WhenDone done);

// ---- event time picker (evtime.c): all day, start and end time, days

typedef struct {
    bool all_day;
    int start;   // minutes since midnight
    int end;     // minutes since midnight, or CAL_NO_END (-1)
    int days;    // extra days the event lasts, 0 = one day
} EvTime;

typedef void (*EvTimeDone)(const EvTime *t);
void evtime_open(const char *text, Date start, const EvTime *init, EvTimeDone done);

// ---- work shift picker (shift.c): the saved shifts with that day's hours

// The index of the saved shift picked, or SHIFT_CUSTOM (store/work.h).
typedef void (*ShiftDone)(int shift);
void shift_open(Date day, ShiftDone done);

// US holidays on `day` (store/holidays.h), or none when Settings has them
// off. Holiday text is drawn in C_PURPLE.
int day_holidays(Date day, const char **out, int max);

// Ink for an event's line: red for birthdays and anniversaries, brown for
// work shifts, black for the rest.
uint8_t event_ink(const CalEvent *e);

// Asks to launch an .nds file with a "Launching..." frame first.
void request_launch(const char *full_path);
// Called each frame from main; performs a pending launch.
void launch_poll(void);

#endif
