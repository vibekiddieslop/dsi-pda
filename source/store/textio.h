// Plain-C file helpers shared by every PDA data file.
// No libnds calls here, so tests/host can compile this file with native gcc.
#ifndef STORE_TEXTIO_H
#define STORE_TEXTIO_H

#include <stdbool.h>
#include <stddef.h>

#define PDA_PATH_MAX 128

// Result of text_load().
typedef enum {
    LOAD_OK = 0,      // Main file read.
    LOAD_FROM_BAK,    // Main file missing or unreadable, X.BAK read instead.
    LOAD_MISSING,     // Neither file exists. Caller starts empty.
    LOAD_ERROR,       // A file exists but could not be read (or exceeds max_len).
} LoadResult;

// Reads the whole file into a new malloc'd, NUL-terminated buffer.
// Returns NULL when the file is missing, unreadable or larger than max_len.
char *text_read_file(const char *path, size_t max_len, size_t *out_len);

// Loads `path`, falling back to the same name with extension .BAK.
// *out is malloc'd (caller frees) when the result is LOAD_OK or LOAD_FROM_BAK.
LoadResult text_load(const char *path, size_t max_len, char **out, size_t *out_len);

// Crash-safe save: writes X.TMP, deletes X.BAK, renames X to X.BAK,
// renames X.TMP to X. A power cut at any step leaves X or X.BAK whole.
bool text_save(const char *path, const char *data, size_t len);

// Replaces the extension of an 8.3 path: ("a/TODO.TXT", ".BAK") -> "a/TODO.BAK".
void text_sibling(const char *path, const char *ext, char *out, size_t out_size);

// Splits text into lines in place. Calls fn(line, ctx) for each line with
// CR/LF stripped. Returns the number of lines visited.
int text_each_line(char *text, void (*fn)(char *line, void *ctx), void *ctx);

// Trims leading and trailing spaces/tabs in place, returns the trimmed start.
char *text_trim(char *s);

// Copies at most max characters of src into dst (max + 1 bytes) as one line
// of a data file: tabs and line breaks become spaces. With bars set, so does
// '|', for a field that is not the last on its line.
void text_copy_clean(char *dst, const char *src, size_t max, bool bars);

// Growable output buffer for serializers.
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    bool failed;      // Set when an allocation failed.
} TextBuf;

void textbuf_init(TextBuf *b);
void textbuf_free(TextBuf *b);
void textbuf_append(TextBuf *b, const char *s);
void textbuf_appendf(TextBuf *b, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif
