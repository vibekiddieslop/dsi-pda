#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <fat.h>
#include <nds.h>

TodoList g_todo;
CalList g_cal;
AlarmList g_alarms;
HabitList g_habits;
ShiftList g_shifts;
Ini g_cfg;

static bool storage_ok = false;
static char drive[8] = "sd:/";
static char notes_dir[PDA_PATH_MAX];
static char last_error[64];

#define MAX_FILE (256 * 1024)

const char *app_drive(void)
{
    return drive;
}

bool app_storage_ok(void)
{
    return storage_ok;
}

const char *app_last_error(void)
{
    return last_error;
}

void app_path(const char *name, char *out, int size)
{
    snprintf(out, (size_t)size, "%spda/%s", drive, name);
}

const char *app_notes_dir(void)
{
    return notes_dir;
}

void app_root_path(const char *rel, char *out, int size)
{
    while (*rel == '/')
        rel++;
    snprintf(out, (size_t)size, "%s%s", drive, rel);
}

static void set_error(const char *fmt, const char *name)
{
    snprintf(last_error, sizeof(last_error), fmt, name);
}

// Loads one file through text_load and hands the text to parse().
static void load_file(const char *name, void (*parse)(char *text))
{
    char p[PDA_PATH_MAX];
    app_path(name, p, sizeof(p));
    char *text = NULL;
    size_t len = 0;
    LoadResult r = text_load(p, MAX_FILE, &text, &len);
    switch (r)
    {
        case LOAD_OK:
            break;
        case LOAD_FROM_BAK:
            set_error("%s: backup loaded.", name);
            break;
        case LOAD_MISSING:
            return;
        case LOAD_ERROR:
            set_error("Read failed: %s", name);
            return;
    }
    parse(text);
    free(text);
}

static void parse_todo(char *t) { todo_parse(&g_todo, t); }
static void parse_cal(char *t) { cal_parse(&g_cal, t); }
static void parse_alarms(char *t) { alarm_parse(&g_alarms, t); }
static void parse_habits(char *t) { habit_parse(&g_habits, t); }
static void parse_shifts(char *t) { shift_parse(&g_shifts, t); }
static void parse_cfg(char *t) { ini_parse(&g_cfg, t); }

static bool save_buf(const char *name, TextBuf *b, bool ok)
{
    if (!storage_ok)
    {
        textbuf_free(b);
        set_error("No SD card. %s not saved.", name);
        return false;
    }
    char p[PDA_PATH_MAX];
    app_path(name, p, sizeof(p));
    ok = ok && text_save(p, b->data ? b->data : "", b->len);
    textbuf_free(b);
    if (!ok)
        set_error("Save failed: %s", name);
    return ok;
}

bool app_save_todo(void)
{
    TextBuf b;
    textbuf_init(&b);
    return save_buf("TODO.TXT", &b, todo_serialize(&g_todo, &b));
}

bool app_save_cal(void)
{
    TextBuf b;
    textbuf_init(&b);
    return save_buf("CAL.TXT", &b, cal_serialize(&g_cal, &b));
}

bool app_save_alarms(void)
{
    TextBuf b;
    textbuf_init(&b);
    return save_buf("ALARMS.TXT", &b, alarm_serialize(&g_alarms, &b));
}

bool app_save_habits(void)
{
    TextBuf b;
    textbuf_init(&b);
    return save_buf("HABITS.TXT", &b, habit_serialize(&g_habits, &b));
}

bool app_save_shifts(void)
{
    TextBuf b;
    textbuf_init(&b);
    return save_buf("SHIFTS.TXT", &b, shift_serialize(&g_shifts, &b));
}

bool app_set_config(const char *section, const char *key, const char *value)
{
    if (!storage_ok)
        return false;
    char p[PDA_PATH_MAX];
    app_path("CONFIG.INI", p, sizeof(p));
    char *old = NULL;
    size_t len = 0;
    LoadResult r = text_load(p, MAX_FILE, &old, &len);
    if (r == LOAD_ERROR)
        return false;
    char *text = ini_text_set(r == LOAD_MISSING ? INI_DEFAULT_CONFIG : old, section, key, value);
    free(old);
    if (text == NULL)
        return false;
    bool ok = text_save(p, text, strlen(text));
    ini_parse(&g_cfg, text);  // Entries are copied, so text can be freed after.
    free(text);
    if (!ok)
        set_error("Save failed: %s", "CONFIG.INI");
    return ok;
}

bool app_init(void)
{
    todo_clear(&g_todo);
    cal_clear(&g_cal);
    alarm_clear(&g_alarms);
    habit_clear(&g_habits);
    shift_clear(&g_shifts);
    g_cfg.count = 0;

    storage_ok = fatInitDefault();
    if (storage_ok)
    {
        const char *d = fatGetDefaultDrive();
        if (d != NULL && d[0] != '\0')
            snprintf(drive, sizeof(drive), "%s", d);
    }
    snprintf(notes_dir, sizeof(notes_dir), "%spda/NOTES", drive);

    if (!storage_ok)
    {
        snprintf(last_error, sizeof(last_error), "No SD card.");
        char *def = strdup(INI_DEFAULT_CONFIG);
        if (def)
        {
            ini_parse(&g_cfg, def);
            free(def);
        }
        return false;
    }

    char p[PDA_PATH_MAX];
    snprintf(p, sizeof(p), "%spda", drive);
    mkdir(p, 0777);
    mkdir(notes_dir, 0777);

    // First run: write a default CONFIG.INI the user can edit on a PC.
    app_path("CONFIG.INI", p, sizeof(p));
    struct stat st;
    if (stat(p, &st) != 0)
        text_save(p, INI_DEFAULT_CONFIG, strlen(INI_DEFAULT_CONFIG));

    load_file("TODO.TXT", parse_todo);
    load_file("CAL.TXT", parse_cal);
    load_file("ALARMS.TXT", parse_alarms);
    load_file("HABITS.TXT", parse_habits);
    load_file("SHIFTS.TXT", parse_shifts);
    load_file("CONFIG.INI", parse_cfg);
    return true;
}
