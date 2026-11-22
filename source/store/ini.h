// Minimal INI reader for CONFIG.INI.
//   [launch]
//   boot=/BOOT.NDS
//   [apps]
//   Moonshell=/apps/msh.nds
// Keys keep file order, so [apps] entries show in the order written.
#ifndef STORE_INI_H
#define STORE_INI_H

#include <stdbool.h>

#define INI_MAX 64
#define INI_SECTION_MAX 24
#define INI_KEY_MAX 40
#define INI_VALUE_MAX 120

typedef struct {
    char section[INI_SECTION_MAX + 1];
    char key[INI_KEY_MAX + 1];
    char value[INI_VALUE_MAX + 1];
} IniEntry;

typedef struct {
    int count;
    IniEntry e[INI_MAX];
} Ini;

void ini_parse(Ini *ini, char *text);
// Section and key compare case-insensitively. Returns def when missing.
const char *ini_get(const Ini *ini, const char *section, const char *key, const char *def);

// Sets key=value in section of an INI file's text, keeping every other line
// (comments, order) as written. Replaces the key's first line in that
// section, or adds the key right after the section header, or appends the
// section at the end. Returns the new text (malloc'd), or NULL on failure.
char *ini_text_set(const char *text, const char *section, const char *key,
                   const char *value);

// The default CONFIG.INI written on first run.
extern const char INI_DEFAULT_CONFIG[];

#endif
