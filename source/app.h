// Shared PDA state: data files, root folder and save helpers.
#ifndef APP_H
#define APP_H

#include <stdbool.h>

#include "store/alarm_io.h"
#include "store/cal_io.h"
#include "store/habit_io.h"
#include "store/ini.h"
#include "store/textio.h"
#include "store/todo_io.h"
#include "store/work.h"

extern TodoList g_todo;
extern CalList g_cal;
extern AlarmList g_alarms;
extern HabitList g_habits;
extern ShiftList g_shifts;   // the work shift types (SHIFTS.TXT)
extern Ini g_cfg;

// Mounts the SD card and loads every data file. Returns false when no
// filesystem is available (the PDA then runs without saving).
bool app_init(void);
bool app_storage_ok(void);

// "sd:/" on the DSi SD slot, "fat:/" under DLDI.
const char *app_drive(void);
// Builds "<drive>pda/<name>".
void app_path(const char *name, char *out, int size);
// "<drive>pda/NOTES"
const char *app_notes_dir(void);
// Builds "<drive><path without leading slash>" for a path from CONFIG.INI.
void app_root_path(const char *rel, char *out, int size);

bool app_save_todo(void);
bool app_save_cal(void);
bool app_save_alarms(void);
bool app_save_habits(void);
bool app_save_shifts(void);
// Writes one key into CONFIG.INI (other lines kept) and reloads g_cfg.
bool app_set_config(const char *section, const char *key, const char *value);

// Text describing the last load/save problem, or "" when there is none.
const char *app_last_error(void);

#endif
