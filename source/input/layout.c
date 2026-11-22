#include "layout.h"

static int row_y(int r)
{
    return KBD_ROWS_Y + r * KBD_ROW_H;
}

// n keys spread over [x0, x1) in row r, typing chars[i].
static int add_row(KbdKey *out, int k, int max, int r, int x0, int x1, const char *chars,
                   int n)
{
    for (int i = 0; i < n && k < max; i++)
    {
        int a = x0 + (x1 - x0) * i / n;
        int b = x0 + (x1 - x0) * (i + 1) / n;
        out[k++] = (KbdKey){ (short)a, (short)row_y(r), (short)(b - a), KBD_ROW_H, KK_CHAR,
                             chars[i] };
    }
    return k;
}

static int add_key(KbdKey *out, int k, int max, int r, int x0, int x1, KeyKind kind, char ch)
{
    if (k < max)
        out[k++] = (KbdKey){ (short)x0, (short)row_y(r), (short)(x1 - x0), KBD_ROW_H, kind, ch };
    return k;
}

int layout_keys(int page, KbdKey *out, int max)
{
    int k = 0;
    if (page == PAGE_LETTERS)
    {
        k = add_row(out, k, max, 0, 0, 256, "qwertyuiop", 10);
        k = add_row(out, k, max, 1, 13, 243, "asdfghjkl", 9);
        k = add_key(out, k, max, 2, 0, 38, KK_SHIFT, 0);
        k = add_row(out, k, max, 2, 38, 218, "zxcvbnm", 7);
        k = add_key(out, k, max, 2, 218, 256, KK_BKSP, 0);
        k = add_key(out, k, max, 3, 0, 36, KK_PAGE, 0);
        k = add_key(out, k, max, 3, 36, 66, KK_PEN, 0);
        k = add_key(out, k, max, 3, 66, 92, KK_CHAR, ',');
        k = add_key(out, k, max, 3, 92, 178, KK_SPACE, ' ');
        k = add_key(out, k, max, 3, 178, 204, KK_CHAR, '.');
        k = add_key(out, k, max, 3, 204, 256, KK_ENTER, 0);
    }
    else
    {
        k = add_row(out, k, max, 0, 0, 256, "1234567890", 10);
        k = add_row(out, k, max, 1, 0, 256, "-/:;()$&@\"", 10);
        k = add_key(out, k, max, 2, 0, 38, KK_CHAR, '=');
        k = add_row(out, k, max, 2, 38, 218, ".,?!'+#", 7);
        k = add_key(out, k, max, 2, 218, 256, KK_BKSP, 0);
        k = add_key(out, k, max, 3, 0, 36, KK_PAGE, 0);
        k = add_key(out, k, max, 3, 36, 66, KK_PEN, 0);
        k = add_key(out, k, max, 3, 66, 92, KK_CHAR, '%');
        k = add_key(out, k, max, 3, 92, 178, KK_SPACE, ' ');
        k = add_key(out, k, max, 3, 178, 204, KK_CHAR, '*');
        k = add_key(out, k, max, 3, 204, 256, KK_ENTER, 0);
    }
    return k;
}

int layout_hit(const KbdKey *keys, int n, int x, int y)
{
    for (int i = 0; i < n; i++)
    {
        const KbdKey *k = &keys[i];
        if (x >= k->x && x < k->x + k->w && y >= k->y && y < k->y + k->h)
            return i;
    }
    return -1;
}

bool layout_letter_center(char c, int *x, int *y)
{
    static KbdKey keys[KBD_MAX_KEYS];
    static int n = 0;
    if (n == 0)
        n = layout_keys(PAGE_LETTERS, keys, KBD_MAX_KEYS);
    for (int i = 0; i < n; i++)
    {
        if (keys[i].kind == KK_CHAR && keys[i].ch == c && c >= 'a' && c <= 'z')
        {
            *x = keys[i].x + keys[i].w / 2;
            *y = keys[i].y + keys[i].h / 2;
            return true;
        }
    }
    return false;
}
