#include "ini.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "textio.h"

const char INI_DEFAULT_CONFIG[] =
    "# PDA launcher settings. Paths start at the SD card root.\n"
    "[launch]\n"
    "boot=/BOOT.NDS\n"
    "\n"
    "# sounds=on/off: beeps and jingles (alarms always ring).\n"
    "# brightness=1..5 is written by L+R+Up/Down (the DSi itself keeps the level).\n"
    "[ui]\n"
    "sounds=on\n"
    "\n"
    "# Apps menu: Name=/path/to/file.nds (up to 20 entries)\n"
    "[apps]\n"
    "TWiLight Menu++=/BOOT.NDS\n";

typedef struct {
    Ini *ini;
    char section[INI_SECTION_MAX + 1];
} ParseCtx;

static void parse_line(char *line, void *vctx)
{
    ParseCtx *ctx = vctx;
    char *s = text_trim(line);
    if (*s == '\0' || *s == '#' || *s == ';')
        return;
    if (*s == '[')
    {
        char *end = strchr(s, ']');
        if (end == NULL)
            return;
        *end = '\0';
        snprintf(ctx->section, sizeof(ctx->section), "%s", text_trim(s + 1));
        return;
    }
    char *eq = strchr(s, '=');
    if (eq == NULL || ctx->ini->count >= INI_MAX)
        return;
    *eq = '\0';
    IniEntry *e = &ctx->ini->e[ctx->ini->count];
    snprintf(e->section, sizeof(e->section), "%s", ctx->section);
    snprintf(e->key, sizeof(e->key), "%s", text_trim(s));
    snprintf(e->value, sizeof(e->value), "%s", text_trim(eq + 1));
    if (e->key[0] != '\0')
        ctx->ini->count++;
}

void ini_parse(Ini *ini, char *text)
{
    ParseCtx ctx = { ini, "" };
    ini->count = 0;
    text_each_line(text, parse_line, &ctx);
}

// Classifies one line (not modified): returns 1 for a "[name]" header (name
// copied to sect), 2 for "key=" with the key copied to k, else 0.
static int classify(const char *line, size_t n, char *sect, char *k)
{
    char tmp[INI_VALUE_MAX + INI_KEY_MAX + 8];
    if (n >= sizeof(tmp))
        n = sizeof(tmp) - 1;
    memcpy(tmp, line, n);
    tmp[n] = '\0';
    char *s = text_trim(tmp);
    if (*s == '[')
    {
        char *end = strchr(s, ']');
        if (end == NULL)
            return 0;
        *end = '\0';
        snprintf(sect, INI_SECTION_MAX + 1, "%s", text_trim(s + 1));
        return 1;
    }
    if (*s == '#' || *s == ';')
        return 0;
    char *eq = strchr(s, '=');
    if (eq == NULL)
        return 0;
    *eq = '\0';
    snprintf(k, INI_KEY_MAX + 1, "%s", text_trim(s));
    return 2;
}

char *ini_text_set(const char *text, const char *section, const char *key,
                   const char *value)
{
    TextBuf b;
    textbuf_init(&b);
    char cur[INI_SECTION_MAX + 1] = "";
    char k[INI_KEY_MAX + 1];
    bool done = false, in_target = false, exists = false;
    const char *p = text ? text : "";

    // Pass 1: does the section already hold the key? Then it is replaced in
    // place; otherwise it goes right after the section header.
    for (const char *q = p; *q;)
    {
        const char *eol = strchr(q, '\n');
        size_t n = eol ? (size_t)(eol - q) : strlen(q);
        int kind = classify(q, n, cur, k);
        if (kind == 1)
            in_target = strcasecmp(cur, section) == 0;
        else if (kind == 2 && in_target && strcasecmp(k, key) == 0)
            exists = true;
        q = eol ? eol + 1 : q + n;
    }
    cur[0] = '\0';
    in_target = false;

    while (*p)
    {
        const char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        size_t content = (n > 0 && p[n - 1] == '\r') ? n - 1 : n;
        int kind = classify(p, content, cur, k);
        if (kind == 1)
            in_target = strcasecmp(cur, section) == 0;

        if (!done && in_target && kind == 2 && strcasecmp(k, key) == 0)
        {
            textbuf_appendf(&b, "%s=%s\n", key, value);
            done = true;
        }
        else
        {
            textbuf_appendf(&b, "%.*s\n", (int)content, p);
            if (!done && !exists && kind == 1 && in_target)
            {
                textbuf_appendf(&b, "%s=%s\n", key, value);
                done = true;
            }
        }
        p = eol ? eol + 1 : p + n;
    }
    if (!done)
    {
        if (b.len > 0 && !(b.len >= 2 && b.data[b.len - 2] == '\n'))
            textbuf_append(&b, "\n");
        textbuf_appendf(&b, "[%s]\n%s=%s\n", section, key, value);
    }
    if (b.failed || b.data == NULL)
    {
        textbuf_free(&b);
        return NULL;
    }
    return b.data;
}

const char *ini_get(const Ini *ini, const char *section, const char *key, const char *def)
{
    for (int i = 0; i < ini->count; i++)
    {
        if (strcasecmp(ini->e[i].section, section) == 0 &&
            strcasecmp(ini->e[i].key, key) == 0)
            return ini->e[i].value;
    }
    return def;
}
