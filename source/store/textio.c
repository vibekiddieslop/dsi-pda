#include "textio.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *text_read_file(const char *path, size_t max_len, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;

    size_t cap = max_len < 1024 ? max_len : 1024, len = 0;
    char *buf = malloc(cap + 1);
    if (buf == NULL)
    {
        fclose(f);
        return NULL;
    }

    for (;;)
    {
        if (len == cap)
        {
            if (cap >= max_len)
            {
                // One more byte means the file is over the limit.
                int c = fgetc(f);
                if (c != EOF)
                    goto fail;
                break;
            }
            size_t ncap = cap * 2;
            if (ncap > max_len)
                ncap = max_len;
            char *nbuf = realloc(buf, ncap + 1);
            if (nbuf == NULL)
                goto fail;
            buf = nbuf;
            cap = ncap;
        }
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0)
        {
            if (ferror(f))
                goto fail;
            break;
        }
    }

    fclose(f);
    buf[len] = '\0';
    if (out_len)
        *out_len = len;
    return buf;

fail:
    fclose(f);
    free(buf);
    return NULL;
}

void text_sibling(const char *path, const char *ext, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", path);
    char *slash = strrchr(out, '/');
    char *dot = strrchr(out, '.');
    if (dot != NULL && (slash == NULL || dot > slash))
        *dot = '\0';
    size_t used = strlen(out);
    if (used < out_size)
        snprintf(out + used, out_size - used, "%s", ext);
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    fclose(f);
    return true;
}

LoadResult text_load(const char *path, size_t max_len, char **out, size_t *out_len)
{
    *out = text_read_file(path, max_len, out_len);
    if (*out != NULL)
        return LOAD_OK;

    bool main_exists = file_exists(path);

    char bak[PDA_PATH_MAX];
    text_sibling(path, ".BAK", bak, sizeof(bak));
    *out = text_read_file(bak, max_len, out_len);
    if (*out != NULL)
        return LOAD_FROM_BAK;

    if (main_exists || file_exists(bak))
        return LOAD_ERROR;
    return LOAD_MISSING;
}

bool text_save(const char *path, const char *data, size_t len)
{
    char tmp[PDA_PATH_MAX], bak[PDA_PATH_MAX];
    text_sibling(path, ".TMP", tmp, sizeof(tmp));
    text_sibling(path, ".BAK", bak, sizeof(bak));

    FILE *f = fopen(tmp, "wb");
    if (f == NULL)
        return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (!ok)
    {
        remove(tmp);
        return false;
    }

    // FatFs rename() fails when the target exists, so delete first.
    remove(bak);
    bool had_main = file_exists(path);
    if (had_main && rename(path, bak) != 0)
    {
        remove(tmp);
        return false;
    }
    if (rename(tmp, path) != 0)
    {
        if (had_main)
            rename(bak, path); // Put the old version back.
        return false;
    }
    return true;
}

int text_each_line(char *text, void (*fn)(char *line, void *ctx), void *ctx)
{
    int count = 0;
    char *p = text;
    while (*p != '\0')
    {
        char *end = strchr(p, '\n');
        char *next;
        if (end != NULL)
        {
            *end = '\0';
            next = end + 1;
        }
        else
        {
            next = p + strlen(p);
        }
        size_t l = strlen(p);
        if (l > 0 && p[l - 1] == '\r')
            p[l - 1] = '\0';
        fn(p, ctx);
        count++;
        p = next;
    }
    return count;
}

char *text_trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t l = strlen(s);
    while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\t'))
        s[--l] = '\0';
    return s;
}

void text_copy_clean(char *dst, const char *src, size_t max, bool bars)
{
    size_t o = 0;
    for (size_t i = 0; src[i] != '\0' && o < max; i++)
    {
        char c = src[i];
        if (c == '\t' || c == '\r' || c == '\n' || (bars && c == '|'))
            c = ' ';
        dst[o++] = c;
    }
    dst[o] = '\0';
}

void textbuf_init(TextBuf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->failed = false;
}

void textbuf_free(TextBuf *b)
{
    free(b->data);
    textbuf_init(b);
}

static bool textbuf_reserve(TextBuf *b, size_t extra)
{
    if (b->failed)
        return false;
    if (b->len + extra + 1 <= b->cap)
        return true;
    size_t ncap = b->cap ? b->cap : 256;
    while (ncap < b->len + extra + 1)
        ncap *= 2;
    char *n = realloc(b->data, ncap);
    if (n == NULL)
    {
        b->failed = true;
        return false;
    }
    b->data = n;
    b->cap = ncap;
    return true;
}

void textbuf_append(TextBuf *b, const char *s)
{
    size_t l = strlen(s);
    if (!textbuf_reserve(b, l))
        return;
    memcpy(b->data + b->len, s, l + 1);
    b->len += l;
}

void textbuf_appendf(TextBuf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char small[256];
    int n = vsnprintf(small, sizeof(small), fmt, ap);
    va_end(ap);
    if (n < 0)
    {
        b->failed = true;
        return;
    }
    if ((size_t)n < sizeof(small))
    {
        textbuf_append(b, small);
        return;
    }
    if (!textbuf_reserve(b, (size_t)n))
        return;
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}
