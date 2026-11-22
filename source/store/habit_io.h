// HABITS.TXT: one habit per line.
//   Stretch|2026-11-14|xx-x-xx
//   Gym|2026-11-02|x--x---x|3/w
// The date is the day of the first history character; each following
// character is the next day, 'x' done, '-' not done. Days after the end of
// the string count as not done. Only the last HABIT_DAYS days are kept.
// An optional 4th field "N/w" makes the goal N times a week (weeks run
// Monday to Sunday); without it the goal is every day.
#ifndef STORE_HABIT_IO_H
#define STORE_HABIT_IO_H

#include <stdbool.h>

#include "date.h"
#include "textio.h"

#define HABIT_MAX 16
#define HABIT_NAME_MAX 30
#define HABIT_DAYS 400

#define HABIT_GOAL_MAX 6          // times a week; 0 = every day

typedef struct {
    char name[HABIT_NAME_MAX + 1];
    int goal;                     // 0 every day, else times a week
    Date start;                   // day of days[0]
    int len;
    char days[HABIT_DAYS + 1];
} Habit;

typedef struct {
    int count;
    Habit h[HABIT_MAX];
} HabitList;

void habit_clear(HabitList *l);
void habit_parse(HabitList *l, char *text);
bool habit_serialize(const HabitList *l, TextBuf *b);

// Returns the new index, or -1 when full or the name is empty.
int habit_add(HabitList *l, const char *name, Date today);
void habit_remove(HabitList *l, int index);
void habit_rename(Habit *h, const char *name);

bool habit_done(const Habit *h, Date day);
void habit_set(Habit *h, Date day, bool done);
// Times done in the Monday-Sunday week holding `day`.
int habit_week_count(const Habit *h, Date day);
// Nothing left to do today: done today, or (weekly goal) the week's goal
// is already met.
bool habit_satisfied(const Habit *h, Date today);
// Current streak, in days for an every-day habit (ending today, or
// yesterday when today is not done yet), in weeks for a weekly goal
// (weeks meeting it, this week counted once it does).
int habit_streak(const Habit *h, Date today);
// Longest streak ever, in the same unit.
int habit_best_streak(const Habit *h, Date today);
// Days done among the last n days (today included).
int habit_done_last(const Habit *h, Date today, int n);
// Next goal for the Goal key: every day -> 1/w -> ... -> 6/w -> every day.
int habit_next_goal(int goal);
// "every day", "3 times a week", "once a week".
void habit_goal_text(int goal, char *out, int size);
// Habits satisfied on `day` (see habit_satisfied).
int habit_done_count(const HabitList *l, Date day);

#endif
