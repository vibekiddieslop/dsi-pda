#include "swipe.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "isqrt.h"
#include "layout.h"

#define N 32                  // Resampled points per path
#define FP 16                 // Fixed-point scale for resampled coordinates
#define START_RADIUS 40       // px: first/last letter keys this close to the ends
#define END_KEYS 3            // nearest keys tried at each end
#define RANK_PENALTY_PX 5     // per rank step (rank 0 = most common)

typedef struct {
    uint32_t off;             // word start in the list
    uint8_t len;
    uint8_t rank;
} Word;

static const char *words_text;
static Word *words;
static int word_count;
static int bucket_start[26 * 26 + 1];   // words sorted by first*26 + last letter

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool is_letter(char c)
{
    c = lower(c);
    return c >= 'a' && c <= 'z';
}

static int bucket_of(const char *w, int len)
{
    return (lower(w[0]) - 'a') * 26 + (lower(w[len - 1]) - 'a');
}

// Letters (any case) and apostrophes ("don't", "Friday"); the first and
// last characters must be letters.
static bool word_ok(const char *w, int len)
{
    if (len < 1 || len > SWIPE_WORD_MAX || !is_letter(w[0]) || !is_letter(w[len - 1]))
        return false;
    for (int i = 0; i < len; i++)
        if (!is_letter(w[i]) && w[i] != '\'')
            return false;
    return true;
}

// Calls fn for each valid "<rank><word>" line.
static void each_line(const char *t, size_t len, void (*fn)(uint32_t off, int wl, int rank))
{
    size_t i = 0;
    while (i < len)
    {
        size_t e = i;
        while (e < len && t[e] != '\n' && t[e] != '\r')
            e++;
        if (e - i >= 2 && t[i] >= '0' && t[i] <= '9' && word_ok(t + i + 1, (int)(e - i - 1)))
            fn((uint32_t)(i + 1), (int)(e - i - 1), t[i] - '0');
        while (e < len && (t[e] == '\n' || t[e] == '\r'))
            e++;
        i = e;
    }
}

static int fill_pos[26 * 26];

static void count_line(uint32_t off, int wl, int rank)
{
    (void)rank;
    bucket_start[bucket_of(words_text + off, wl) + 1]++;
}

static void store_line(uint32_t off, int wl, int rank)
{
    int b = bucket_of(words_text + off, wl);
    words[fill_pos[b]++] = (Word){ off, (uint8_t)wl, (uint8_t)rank };
}

bool swipe_load(const char *list, size_t len)
{
    free(words);
    words = NULL;
    word_count = 0;
    words_text = list;
    memset(bucket_start, 0, sizeof(bucket_start));
    each_line(list, len, count_line);
    for (int b = 0; b < 26 * 26; b++)
        bucket_start[b + 1] += bucket_start[b];
    word_count = bucket_start[26 * 26];
    words = malloc(sizeof(Word) * (size_t)(word_count ? word_count : 1));
    if (words == NULL)
    {
        word_count = 0;
        memset(bucket_start, 0, sizeof(bucket_start));
        return false;
    }
    memcpy(fill_pos, bucket_start, sizeof(fill_pos));
    each_line(list, len, store_line);
    return true;
}

int swipe_word_count(void)
{
    return word_count;
}

static int dist_fp(int ax, int ay, int bx, int by)
{
    int dx = ax - bx, dy = ay - by;
    return (int)isqrt((uint32_t)(dx * dx + dy * dy));
}

// Resamples a polyline (pixel coordinates) to N evenly spaced points in
// fixed point. Returns the path length in fixed point.
static int resample(const int *xs, const int *ys, int m, int *ox, int *oy)
{
    int seg[SWIPE_PATH_MAX];
    int total = 0;
    for (int i = 1; i < m; i++)
    {
        seg[i] = dist_fp(xs[i] * FP, ys[i] * FP, xs[i - 1] * FP, ys[i - 1] * FP);
        total += seg[i];
    }
    if (m == 1 || total == 0)
    {
        for (int k = 0; k < N; k++)
        {
            ox[k] = xs[0] * FP;
            oy[k] = ys[0] * FP;
        }
        return 0;
    }
    int i = 1, walked = 0;   // length before segment i
    for (int k = 0; k < N; k++)
    {
        int target = (int)((int64_t)total * k / (N - 1));
        while (i < m - 1 && walked + seg[i] < target)
        {
            walked += seg[i];
            i++;
        }
        int into = target - walked;
        int s = seg[i] ? seg[i] : 1;
        if (into > s)
            into = s;
        ox[k] = xs[i - 1] * FP + (xs[i] - xs[i - 1]) * FP * into / s;
        oy[k] = ys[i - 1] * FP + (ys[i] - ys[i - 1]) * FP * into / s;
    }
    return total;
}

typedef struct {
    int score;
    int idx;
} Cand;

// Letters whose key centers lie within START_RADIUS of (x, y), nearest first.
static int near_letters(int x, int y, int *out)
{
    int n = 0, d[26];
    for (char c = 'a'; c <= 'z'; c++)
    {
        int cx, cy;
        layout_letter_center(c, &cx, &cy);
        int dd = (cx - x) * (cx - x) + (cy - y) * (cy - y);
        if (dd > START_RADIUS * START_RADIUS)
            continue;
        int j = n++;
        while (j > 0 && d[j - 1] > dd)
        {
            d[j] = d[j - 1];
            out[j] = out[j - 1];
            j--;
        }
        d[j] = dd;
        out[j] = c - 'a';
    }
    return n < END_KEYS ? n : END_KEYS;
}

int swipe_decode(const SwipePt *path, int n, char out[][SWIPE_WORD_MAX + 1], int max_out)
{
    if (n < 2 || word_count == 0 || max_out <= 0)
        return 0;
    if (n > SWIPE_PATH_MAX)
        n = SWIPE_PATH_MAX;

    static int ux[SWIPE_PATH_MAX], uy[SWIPE_PATH_MAX];
    for (int i = 0; i < n; i++)
    {
        ux[i] = path[i].x;
        uy[i] = path[i].y;
    }
    int px[N], py[N];
    int user_len = resample(ux, uy, n, px, py);

    int first[26], last[26];
    int nf = near_letters(path[0].x, path[0].y, first);
    int nl = near_letters(path[n - 1].x, path[n - 1].y, last);

    Cand best[8];
    int nbest = 0;
    if (max_out > 8)
        max_out = 8;

    for (int a = 0; a < nf; a++)
    {
        for (int b = 0; b < nl; b++)
        {
            int bucket = first[a] * 26 + last[b];
            for (int w = bucket_start[bucket]; w < bucket_start[bucket + 1]; w++)
            {
                const Word *wd = &words[w];
                const char *s = words_text + wd->off;
                // Key centers, repeated letters once ("letter" -> l e t e r).
                int kx[SWIPE_WORD_MAX], ky[SWIPE_WORD_MAX], m = 0;
                int ideal_len = 0;
                char prev = 0;
                for (int i = 0; i < wd->len; i++)
                {
                    char c = lower(s[i]);
                    if (c == '\'' || c == prev)
                        continue;   // Apostrophes have no key here.
                    prev = c;
                    layout_letter_center(c, &kx[m], &ky[m]);
                    if (m > 0)
                        ideal_len += dist_fp(kx[m] * FP, ky[m] * FP, kx[m - 1] * FP,
                                             ky[m - 1] * FP);
                    m++;
                }
                if (m < 2)
                    continue;  // One key: that is a tap, not a swipe.
                // Strokes much longer or shorter than the word's path are
                // not that word.
                if (ideal_len > user_len * 2 + 40 * FP || user_len > ideal_len * 2 + 40 * FP)
                    continue;

                int qx[N], qy[N];
                resample(kx, ky, m, qx, qy);
                int sum = 0;
                for (int k = 0; k < N; k++)
                    sum += dist_fp(px[k], py[k], qx[k], qy[k]);
                // Strokes start and end on the word's first and last keys
                // far more precisely than they follow the middle, so the
                // ends count extra.
                int ends = dist_fp(px[0], py[0], qx[0], qy[0]) +
                           dist_fp(px[N - 1], py[N - 1], qx[N - 1], qy[N - 1]);
                int score = sum / N + ends / 2 + wd->rank * RANK_PENALTY_PX * FP;

                if (nbest == max_out && score >= best[nbest - 1].score)
                    continue;
                int j = nbest < max_out ? nbest++ : nbest - 1;
                while (j > 0 && best[j - 1].score > score)
                {
                    best[j] = best[j - 1];
                    j--;
                }
                best[j] = (Cand){ score, w };
            }
        }
    }

    for (int i = 0; i < nbest; i++)
    {
        const Word *wd = &words[best[i].idx];
        memcpy(out[i], words_text + wd->off, wd->len);
        out[i][wd->len] = '\0';
    }
    return nbest;
}
