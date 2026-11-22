#include "notes_io.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void note_path(const char *dir, int id, char *out, size_t size)
{
    snprintf(out, size, "%s/N%04d.TXT", dir, id);
}

void sketch_path(const char *dir, int id, char *out, size_t size)
{
    snprintf(out, size, "%s/S%04d.BMP", dir, id);
}

void checklist_path(const char *dir, int id, char *out, size_t size)
{
    snprintf(out, size, "%s/L%04d.TXT", dir, id);
}

const char *checklist_item(const char *line, bool *done)
{
    *done = false;
    if (line[0] == '[' && (line[1] == ' ' || line[1] == 'x' || line[1] == 'X') && line[2] == ']')
    {
        *done = line[1] != ' ';
        line += 3;
        if (*line == ' ')
            line++;
    }
    return line;
}

void checklist_line(bool done, const char *text, char *out, size_t size)
{
    snprintf(out, size, "[%c] %s", done ? 'x' : ' ', text);
}

void bills_path(const char *dir, int id, char *out, size_t size)
{
    snprintf(out, size, "%s/B%04d.TXT", dir, id);
}

bool money_parse(const char *s, long *cents)
{
    while (*s == ' ')
        s++;
    bool neg = false;
    if (*s == '-')
        neg = true, s++;
    if (*s == '$')
        s++;
    if (!neg && *s == '-')   // "$-3"
        neg = true, s++;
    long whole = 0;
    int digits = 0;
    for (; (*s >= '0' && *s <= '9') || (*s == ',' && digits > 0); s++)
    {
        if (*s == ',')
            continue;
        whole = whole * 10 + (*s - '0');
        if (++digits > 7)
            return false;
    }
    long frac = 0;
    int fd = 0;
    if (*s == '.')
    {
        for (s++; *s >= '0' && *s <= '9'; s++)
        {
            if (++fd > 2)
                return false;
            frac = frac * 10 + (*s - '0');
        }
        if (fd == 1)
            frac *= 10;
    }
    while (*s == ' ')
        s++;
    if (*s != '\0' || (digits == 0 && fd == 0))
        return false;
    long v = whole * 100 + frac;
    *cents = neg ? -v : v;
    return true;
}

void money_format(long long cents, char *out, size_t size)
{
    bool neg = cents < 0;
    unsigned long long v = (unsigned long long)(neg ? -cents : cents);
    // Whole dollars with a comma every three digits, written backwards.
    char rev[32];
    int n = 0;
    unsigned long long whole = v / 100;
    do
    {
        if (n > 0 && n % 4 == 3)
            rev[n++] = ',';
        rev[n++] = (char)('0' + whole % 10);
        whole /= 10;
    } while (whole > 0 && n < 28);
    char dollars[32];
    for (int i = 0; i < n; i++)
        dollars[i] = rev[n - 1 - i];
    dollars[n] = '\0';
    snprintf(out, size, "%s$%s.%02u", neg ? "-" : "", dollars, (unsigned)(v % 100));
}

void bill_item(const char *line, char *text, size_t text_size, long *cents)
{
    *cents = 0;
    const char *bar = strrchr(line, '|');
    if (bar != NULL && money_parse(bar + 1, cents))
    {
        size_t n = (size_t)(bar - line);
        while (n > 0 && line[n - 1] == ' ')
            n--;
        snprintf(text, text_size, "%.*s", (int)n, line);
        return;
    }
    snprintf(text, text_size, "%s", line);
}

void bill_line(const char *text, long cents, char *out, size_t size)
{
    long v = cents < 0 ? -cents : cents;
    snprintf(out, size, "%s|%s%ld.%02ld", text, cents < 0 ? "-" : "", v / 100, v % 100);
}

bool bill_split(const char *input, char *text, size_t text_size, long *cents)
{
    const char *sp = strrchr(input, ' ');
    if (sp == NULL || !money_parse(sp + 1, cents))
        return false;
    size_t n = (size_t)(sp - input);
    while (n > 0 && input[n - 1] == ' ')
        n--;
    if (n == 0)
        return false;
    snprintf(text, text_size, "%.*s", (int)n, input);
    return true;
}

long long bills_total(const NoteDoc *d)
{
    long long sum = 0;
    char text[NOTE_LINE_MAX + 1];
    for (int i = 1; i < d->count; i++)
    {
        long c;
        bill_item(d->lines[i], text, sizeof(text), &c);
        sum += c;
    }
    return sum;
}

// Matches "<letter>0001.<ext>" (any case, ext lowercase here) and returns
// the id, or -1.
static int parse_name(const char *name, char letter, const char *ext3)
{
    if (strlen(name) != 9)
        return -1;
    if ((name[0] | 32) != letter)
        return -1;
    int id = 0;
    for (int i = 1; i <= 4; i++)
    {
        if (name[i] < '0' || name[i] > '9')
            return -1;
        id = id * 10 + (name[i] - '0');
    }
    if (name[5] != '.')
        return -1;
    const char *ext = name + 6;
    if ((ext[0] | 32) != ext3[0] || (ext[1] | 32) != ext3[1] || (ext[2] | 32) != ext3[2])
        return -1;
    return id > 0 ? id : -1;
}

static int cmp_note(const void *a, const void *b)
{
    return ((const NoteInfo *)a)->id - ((const NoteInfo *)b)->id;
}

// Adds up the glance numbers for the list (the caller zeroes them): a
// checklist's items and how many are checked, or the bills' total. Reads
// the whole file (NOTE_MAX_BYTES).
static void read_summary(const char *path, NoteInfo *n)
{
    size_t len = 0;
    char *text = text_read_file(path, NOTE_MAX_BYTES, &len);
    if (text == NULL)
        return;
    char *line = strchr(text, '\n');   // line 1 on; line 0 is the title
    while (line != NULL)
    {
        line++;
        char *end = strchr(line, '\n');
        if (end != NULL)
            *end = '\0';
        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\r')
            line[--l] = '\0';
        if (l > 0 && n->checklist)
        {
            bool done;
            checklist_item(line, &done);
            n->items++;
            n->done += done;
        }
        else if (l > 0 && n->bills)
        {
            char name[NOTE_LINE_MAX + 1];
            long c;
            bill_item(line, name, sizeof(name), &c);
            n->total += c;
        }
        line = end;
    }
    free(text);
}

static void read_title(const char *path, char *title)
{
    title[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return;
    char line[NOTE_LINE_MAX + 2];
    if (fgets(line, sizeof(line), f) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        snprintf(title, NOTE_TITLE_MAX + 1, "%s", text_trim(line));
    }
    fclose(f);
}

bool note_index_load(NoteIndex *idx, const char *dir)
{
    idx->count = 0;
    DIR *d = opendir(dir);
    if (d == NULL)
        return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && idx->count < NOTES_MAX)
    {
        bool sketch = false, checklist = false, bills = false;
        int id = parse_name(e->d_name, 'n', "txt");
        if (id < 0 && (id = parse_name(e->d_name, 's', "bmp")) >= 0)
            sketch = true;
        if (id < 0 && (id = parse_name(e->d_name, 'l', "txt")) >= 0)
            checklist = true;
        if (id < 0 && (id = parse_name(e->d_name, 'b', "txt")) >= 0)
            bills = true;
        if (id < 0)
            continue;
        idx->n[idx->count].id = id;
        idx->n[idx->count].sketch = sketch;
        idx->n[idx->count].checklist = checklist;
        idx->n[idx->count].bills = bills;
        idx->count++;
    }
    closedir(d);
    qsort(idx->n, (size_t)idx->count, sizeof(NoteInfo), cmp_note);

    for (int i = 0; i < idx->count; i++)
    {
        NoteInfo *n = &idx->n[i];
        char p[PDA_PATH_MAX];
        if (n->sketch)
        {
            char bmp[PDA_PATH_MAX];
            sketch_path(dir, n->id, bmp, sizeof(bmp));
            text_sibling(bmp, ".TXT", p, sizeof(p));
        }
        else if (n->checklist)
        {
            checklist_path(dir, n->id, p, sizeof(p));
        }
        else if (n->bills)
        {
            bills_path(dir, n->id, p, sizeof(p));
        }
        else
        {
            note_path(dir, n->id, p, sizeof(p));
        }
        read_title(p, n->title);
        n->items = n->done = 0;
        n->total = 0;
        if (n->checklist || n->bills)
            read_summary(p, n);
        if (n->title[0] == '\0')
            snprintf(n->title, NOTE_TITLE_MAX + 1,
                     n->sketch      ? "Sketch %d"
                     : n->checklist ? "List %d"
                     : n->bills     ? "Bills %d"
                                    : "(untitled %d)",
                     n->id);
    }
    return true;
}

int note_next_id(const NoteIndex *idx)
{
    int id = 1;
    for (int i = 0; i < idx->count; i++)
    {
        if (idx->n[i].id == id)
            id++;
        else if (idx->n[i].id > id)
            break;
    }
    return id <= 9999 ? id : -1;
}

void note_doc_init(NoteDoc *d)
{
    d->count = 0;
}

void note_doc_free(NoteDoc *d)
{
    for (int i = 0; i < d->count; i++)
        free(d->lines[i]);
    d->count = 0;
}

static char *dup_line(const char *s, size_t len)
{
    if (len > NOTE_LINE_MAX)
        len = NOTE_LINE_MAX;
    char *p = malloc(len + 1);
    if (p == NULL)
        return NULL;
    for (size_t i = 0; i < len; i++)
        p[i] = (s[i] == '\t') ? ' ' : s[i];
    p[len] = '\0';
    return p;
}

bool note_doc_from_text(NoteDoc *d, const char *text)
{
    note_doc_free(d);
    const char *p = text;
    while (*p != '\0' && d->count < NOTE_MAX_LINES)
    {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        size_t l = len;
        if (l > 0 && p[l - 1] == '\r')
            l--;
        char *line = dup_line(p, l);
        if (line == NULL)
            return false;
        d->lines[d->count++] = line;
        if (end == NULL)
            break;
        p = end + 1;
    }
    return true;
}

bool note_doc_to_text(const NoteDoc *d, TextBuf *b)
{
    for (int i = 0; i < d->count; i++)
    {
        textbuf_append(b, d->lines[i]);
        textbuf_append(b, "\n");
    }
    if (b->data == NULL)
        textbuf_append(b, "");
    return !b->failed;
}

size_t note_doc_size(const NoteDoc *d)
{
    size_t n = 0;
    for (int i = 0; i < d->count; i++)
        n += strlen(d->lines[i]) + 1;
    return n;
}

bool note_doc_insert(NoteDoc *d, int at, const char *line)
{
    if (d->count >= NOTE_MAX_LINES || at < 0 || at > d->count)
        return false;
    size_t len = strlen(line);
    if (note_doc_size(d) + len + 1 > NOTE_MAX_BYTES)
        return false;
    char *p = dup_line(line, len);
    if (p == NULL)
        return false;
    memmove(&d->lines[at + 1], &d->lines[at], sizeof(char *) * (size_t)(d->count - at));
    d->lines[at] = p;
    d->count++;
    return true;
}

bool note_doc_set(NoteDoc *d, int at, const char *line)
{
    if (at < 0 || at >= d->count)
        return false;
    size_t len = strlen(line);
    if (note_doc_size(d) - strlen(d->lines[at]) + len > NOTE_MAX_BYTES)
        return false;
    char *p = dup_line(line, len);
    if (p == NULL)
        return false;
    free(d->lines[at]);
    d->lines[at] = p;
    return true;
}

void note_doc_delete(NoteDoc *d, int at)
{
    if (at < 0 || at >= d->count)
        return;
    free(d->lines[at]);
    memmove(&d->lines[at], &d->lines[at + 1], sizeof(char *) * (size_t)(d->count - at - 1));
    d->count--;
}
