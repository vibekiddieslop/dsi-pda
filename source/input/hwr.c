#include "hwr.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "isqrt.h"

#define N 32          // points per cloud
#define SCALE 1000    // normalized size of the longer bounding box side
#define FP 16         // fixed point for resampling

// ---------------------------------------------------------------- templates
//
// Shapes on a 0..100 grid (y down; descenders go below 100). Strokes are
// separated by '/'. Upper and lower case forms map to the same lowercase
// letter; the pad decides the case. Letters whose capital and small forms
// look alike (c o s v w x z) have one template.

typedef struct {
    char ch;
    unsigned char set;
    const char *shape;
} TemplateSrc;

static const TemplateSrc SRC[] = {
    // Capitals
    { 'a', HWR_LETTERS, "0,100 50,0 100,100 / 25,55 75,55" },
    { 'b', HWR_LETTERS, "10,0 10,100 / 10,0 60,0 80,10 80,40 60,50 10,50 / 10,50 65,50 90,62 90,88 65,100 10,100" },
    { 'c', HWR_LETTERS, "90,15 70,2 40,0 15,15 2,50 15,85 40,100 70,98 90,85" },
    { 'd', HWR_LETTERS, "10,0 10,100 / 10,0 50,0 85,20 95,50 85,80 50,100 10,100" },
    { 'e', HWR_LETTERS, "90,0 10,0 10,100 90,100 / 10,50 70,50" },
    { 'f', HWR_LETTERS, "90,0 10,0 10,100 / 10,50 70,50" },
    { 'g', HWR_LETTERS, "90,15 70,2 40,0 15,15 2,50 15,85 40,100 70,98 90,80 90,55 55,55" },
    { 'h', HWR_LETTERS, "10,0 10,100 / 90,0 90,100 / 10,50 90,50" },
    { 'i', HWR_LETTERS, "50,0 50,100 / 20,0 80,0 / 20,100 80,100" },
    { 'j', HWR_LETTERS, "70,0 70,75 55,95 30,100 10,85" },
    { 'k', HWR_LETTERS, "10,0 10,100 / 85,0 10,60 / 35,45 90,100" },
    { 'l', HWR_LETTERS, "10,0 10,100 90,100" },
    { 'm', HWR_LETTERS, "5,100 10,0 50,60 90,0 95,100" },
    { 'n', HWR_LETTERS, "10,100 10,0 90,100 90,0" },
    { 'o', HWR_LETTERS, "50,0 15,15 0,50 15,85 50,100 85,85 100,50 85,15 50,0" },
    { 'p', HWR_LETTERS, "10,100 10,0 / 10,0 65,0 90,15 90,40 65,55 10,55" },
    { 'q', HWR_LETTERS, "50,0 15,15 0,50 15,85 50,100 85,85 100,50 85,15 50,0 / 60,70 100,105" },
    { 'r', HWR_LETTERS, "10,100 10,0 / 10,0 65,0 90,15 90,40 65,55 10,55 / 45,55 90,100" },
    { 's', HWR_LETTERS, "85,12 60,0 35,0 12,15 15,38 50,50 85,62 90,85 65,100 35,100 10,88" },
    { 't', HWR_LETTERS, "0,0 100,0 / 50,0 50,100" },
    { 'u', HWR_LETTERS, "10,0 10,70 25,95 50,100 75,95 90,70 90,0" },
    { 'v', HWR_LETTERS, "0,0 50,100 100,0" },
    { 'w', HWR_LETTERS, "0,0 25,100 50,35 75,100 100,0" },
    { 'x', HWR_LETTERS, "10,0 90,100 / 90,0 10,100" },
    { 'y', HWR_LETTERS, "10,0 50,50 / 90,0 50,50 50,100" },
    { 'z', HWR_LETTERS, "10,0 90,0 10,100 90,100" },
    // Small letters that differ from their capitals
    { 'a', HWR_LETTERS, "80,50 60,40 35,42 20,60 22,85 40,100 60,95 80,75 / 80,40 80,100" },
    { 'b', HWR_LETTERS, "15,0 15,100 / 15,65 35,45 60,42 80,55 85,75 75,95 50,100 25,95 15,85" },
    { 'd', HWR_LETTERS, "85,0 85,100 / 85,60 65,45 40,42 20,55 15,75 25,95 50,100 70,95 85,85" },
    { 'e', HWR_LETTERS, "15,70 85,70 80,52 60,40 35,42 18,58 15,78 28,95 50,100 70,98 85,88" },
    { 'f', HWR_LETTERS, "75,5 60,0 45,5 38,20 38,100 / 15,45 70,45" },
    { 'g', HWR_LETTERS, "80,50 60,40 35,42 20,60 22,80 40,92 60,88 80,70 / 80,40 80,125 65,140 40,142 20,130" },
    { 'h', HWR_LETTERS, "15,0 15,100 / 15,65 35,45 60,42 80,55 82,75 82,100" },
    { 'i', HWR_LETTERS, "50,40 50,100 / 50,15 50,17" },
    { 'j', HWR_LETTERS, "60,40 60,125 48,140 25,140 12,128 / 60,15 60,17" },
    { 'k', HWR_LETTERS, "15,0 15,100 / 75,40 15,78 / 35,65 80,100" },
    { 'l', HWR_LETTERS, "50,0 50,100" },
    { 'm', HWR_LETTERS, "10,40 10,100 / 10,58 25,42 40,44 48,58 48,100 / 48,58 62,42 80,44 90,58 90,100" },
    { 'n', HWR_LETTERS, "15,40 15,100 / 15,60 35,44 60,42 80,55 82,75 82,100" },
    { 'p', HWR_LETTERS, "15,40 15,140 / 15,55 35,42 60,40 80,52 85,72 75,92 50,100 25,95 15,85" },
    { 'q', HWR_LETTERS, "85,40 85,140 / 85,55 65,42 40,40 20,52 15,72 25,92 50,100 75,95 85,85" },
    { 'r', HWR_LETTERS, "20,40 20,100 / 20,65 35,48 55,40 75,42" },
    { 't', HWR_LETTERS, "45,10 45,90 55,100 70,98 / 20,40 75,40" },
    { 'u', HWR_LETTERS, "15,40 15,80 25,95 45,100 65,95 80,80 / 80,40 80,100" },
    { 'y', HWR_LETTERS, "15,40 45,95 / 80,40 30,140" },
    { 'y', HWR_LETTERS, "15,40 15,75 30,92 50,95 80,75 / 80,40 80,125 65,140 40,140 20,128" },
    { '?', HWR_LETTERS, "15,20 30,3 55,0 80,10 85,30 70,48 50,58 50,75 / 50,95 50,97" },
    { '!', HWR_LETTERS, "50,0 50,72 / 50,95 50,97" },
    // Digits and number punctuation
    { '0', HWR_DIGITS, "50,0 25,10 15,50 25,90 50,100 75,90 85,50 75,10 50,0" },
    { '1', HWR_DIGITS, "30,20 55,0 55,100" },
    { '1', HWR_DIGITS, "50,0 50,100" },
    { '2', HWR_DIGITS, "15,20 30,5 55,0 80,10 85,30 75,50 10,100 90,100" },
    { '3', HWR_DIGITS, "15,10 40,0 70,2 85,18 80,38 55,50 / 55,50 85,62 90,82 70,100 40,100 15,90" },
    { '4', HWR_DIGITS, "70,100 70,0 10,70 95,70" },
    { '4', HWR_DIGITS, "15,0 10,60 90,60 / 70,30 70,100" },
    { '5', HWR_DIGITS, "85,0 20,0 15,45 45,38 75,45 88,65 80,90 55,100 30,100 10,90" },
    { '6', HWR_DIGITS, "80,5 55,0 30,10 12,40 10,70 25,95 50,100 75,92 85,72 75,52 50,45 25,52 12,70" },
    { '7', HWR_DIGITS, "10,0 90,0 40,100" },
    { '8', HWR_DIGITS, "50,50 20,35 20,12 50,0 80,12 80,35 50,50 15,68 15,90 50,100 85,90 85,68 50,50" },
    { '9', HWR_DIGITS, "85,30 70,8 50,0 25,8 15,30 25,50 50,55 75,45 85,30 85,60 75,100" },
    { '-', HWR_DIGITS, "10,50 90,50" },
    { '+', HWR_DIGITS, "10,50 90,50 / 50,10 50,90" },
    { '/', HWR_DIGITS, "85,0 15,100" },
    { ':', HWR_DIGITS, "50,25 50,27 / 50,75 50,77" },
};

#define TEMPLATE_COUNT ((int)(sizeof(SRC) / sizeof(SRC[0])))

typedef struct {
    char ch;
    unsigned char set;
    short x[N], y[N];
} Template;

static Template templates[TEMPLATE_COUNT];
static bool ready = false;

// ---------------------------------------------------------------- ink

// Width and height of the ink, in pixels.
static void bbox(const InkPt *p, int n, int *w, int *h)
{
    int minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
    for (int i = 1; i < n; i++)
    {
        if (p[i].x < minx) minx = p[i].x;
        if (p[i].x > maxx) maxx = p[i].x;
        if (p[i].y < miny) miny = p[i].y;
        if (p[i].y > maxy) maxy = p[i].y;
    }
    *w = maxx - minx;
    *h = maxy - miny;
}

// One tiny stroke: that is '.', found by its size and not by its shape.
static bool is_dot(const InkPt *p, int n)
{
    int w, h;
    bbox(p, n, &w, &h);
    return p[n - 1].stroke == 0 && w < 7 && h < 7;
}

// ---------------------------------------------------------------- cloud

// Resamples the strokes to N points. Each stroke gets at least 2 points
// (so dots count), the rest are shared by length. Output is fixed point.
static void resample(const InkPt *p, int n, int *ox, int *oy)
{
    int start[HWR_MAX_STROKES + 1], len[HWR_MAX_STROKES];
    int ns = 0;
    for (int i = 0; i < n && ns < HWR_MAX_STROKES; i++)
    {
        if (i == 0 || p[i].stroke != p[i - 1].stroke)
            start[ns++] = i;
    }
    start[ns] = n;
    if (ns * 2 > N)
        ns = N / 2;

    int total = 0;
    for (int s = 0; s < ns; s++)
    {
        len[s] = 0;
        for (int i = start[s] + 1; i < start[s + 1]; i++)
        {
            int dx = (p[i].x - p[i - 1].x) * FP, dy = (p[i].y - p[i - 1].y) * FP;
            len[s] += (int)isqrt((uint32_t)(dx * dx + dy * dy));
        }
        total += len[s];
    }

    // Points per stroke: 2 each, the remainder by length.
    int count[HWR_MAX_STROKES], given = 0;
    int spare = N - 2 * ns;
    for (int s = 0; s < ns; s++)
    {
        count[s] = 2 + (total > 0 ? (int)((int64_t)spare * len[s] / total) : 0);
        given += count[s];
    }
    for (int s = 0; given < N; s = (s + 1) % ns, given++)
        count[s]++;

    int o = 0;
    for (int s = 0; s < ns; s++)
    {
        int a = start[s], b = start[s + 1];   // points a..b-1
        int c = count[s];
        int walked = 0, i = a + 1;
        for (int k = 0; k < c; k++)
        {
            int target = (int)((int64_t)len[s] * k / (c - 1));
            int seg = 0;
            while (i < b)
            {
                int dx = (p[i].x - p[i - 1].x) * FP, dy = (p[i].y - p[i - 1].y) * FP;
                seg = (int)isqrt((uint32_t)(dx * dx + dy * dy));
                if (walked + seg >= target || i == b - 1)
                    break;
                walked += seg;
                i++;
            }
            if (b - a < 2 || seg == 0)
            {
                int j = b - a < 2 ? a : i;
                ox[o] = p[j].x * FP;
                oy[o] = p[j].y * FP;
            }
            else
            {
                int into = target - walked;
                if (into > seg)
                    into = seg;
                ox[o] = p[i - 1].x * FP + (p[i].x - p[i - 1].x) * FP * into / seg;
                oy[o] = p[i - 1].y * FP + (p[i].y - p[i - 1].y) * FP * into / seg;
            }
            o++;
        }
    }
}

// Scales so the longer bounding-box side is SCALE, then centers on the
// centroid.
static void normalize(const int *ix, const int *iy, short *ox, short *oy)
{
    int minx = ix[0], maxx = ix[0], miny = iy[0], maxy = iy[0];
    for (int k = 1; k < N; k++)
    {
        if (ix[k] < minx) minx = ix[k];
        if (ix[k] > maxx) maxx = ix[k];
        if (iy[k] < miny) miny = iy[k];
        if (iy[k] > maxy) maxy = iy[k];
    }
    int size = maxx - minx > maxy - miny ? maxx - minx : maxy - miny;
    if (size < 1)
        size = 1;
    int sx = 0, sy = 0;
    int tx[N], ty[N];
    for (int k = 0; k < N; k++)
    {
        tx[k] = (int)((int64_t)(ix[k] - minx) * SCALE / size);
        ty[k] = (int)((int64_t)(iy[k] - miny) * SCALE / size);
        sx += tx[k];
        sy += ty[k];
    }
    sx /= N;
    sy /= N;
    for (int k = 0; k < N; k++)
    {
        ox[k] = (short)(tx[k] - sx);
        oy[k] = (short)(ty[k] - sy);
    }
}

// Greedy matching of cloud a onto cloud b starting at a[start]; earlier
// matches weigh more (the $P weighting).
static uint32_t cloud_distance(const short *ax, const short *ay, const short *bx,
                               const short *by, int start)
{
    bool used[N];
    memset(used, 0, sizeof(used));
    uint32_t sum = 0;
    int i = start;
    for (int k = 0; k < N; k++)
    {
        int best = -1;
        int32_t best_d = INT32_MAX;
        for (int j = 0; j < N; j++)
        {
            if (used[j])
                continue;
            int32_t dx = ax[i] - bx[j], dy = ay[i] - by[j];
            int32_t d = dx * dx + dy * dy;
            if (d < best_d)
            {
                best_d = d;
                best = j;
            }
        }
        used[best] = true;
        sum += (uint32_t)(N - k) * isqrt((uint32_t)best_d);
        i = (i + 1) % N;
    }
    return sum;
}

static uint32_t match(const short *ax, const short *ay, const Template *t)
{
    const int step = 5;   // about N^0.5, as in $P
    uint32_t best = UINT32_MAX;
    for (int i = 0; i < N; i += step)
    {
        uint32_t d1 = cloud_distance(ax, ay, t->x, t->y, i);
        uint32_t d2 = cloud_distance(t->x, t->y, ax, ay, i);
        if (d1 < best) best = d1;
        if (d2 < best) best = d2;
    }
    return best;
}

// ---------------------------------------------------------------- setup

// Parses "x,y x,y / x,y" into points.
static int parse_shape(const char *s, InkPt *out, int max)
{
    int n = 0, stroke = 0;
    while (*s && n < max)
    {
        if (*s == '/')
        {
            stroke++;
            s++;
            continue;
        }
        if (*s < '0' || *s > '9')
        {
            s++;
            continue;
        }
        int x = (int)strtol(s, (char **)&s, 10);
        if (*s != ',')
            break;
        s++;
        int y = (int)strtol(s, (char **)&s, 10);
        // Templates are drawn on 0..100; scale up so short segments still
        // resample smoothly. Normalization removes the size again.
        out[n++] = (InkPt){ (short)(x * 2), (short)(y * 2), (unsigned char)stroke };
    }
    return n;
}

void hwr_init(void)
{
    InkPt pts[64];
    int rx[N], ry[N];
    for (int t = 0; t < TEMPLATE_COUNT; t++)
    {
        int n = parse_shape(SRC[t].shape, pts, 64);
        resample(pts, n, rx, ry);
        normalize(rx, ry, templates[t].x, templates[t].y);
        templates[t].ch = SRC[t].ch;
        templates[t].set = SRC[t].set;
    }
    ready = true;
}

// ---------------------------------------------------------------- user

// The user's own samples, oldest first.
static Template user_t[HWR_USER_MAX];
static int user_n = 0;
static bool user_changed = false;

int hwr_set_of(char ch)
{
    if ((ch >= 'a' && ch <= 'z') || ch == '?' || ch == '!')
        return HWR_LETTERS;
    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '/' || ch == ':')
        return HWR_DIGITS;
    return -1;
}

int hwr_user_count(char ch)
{
    int n = 0;
    for (int i = 0; i < user_n; i++)
        n += user_t[i].ch == ch;
    return n;
}

int hwr_user_total(void)
{
    return user_n;
}

bool hwr_user_dirty(void)
{
    return user_changed;
}

static void user_remove(int i)
{
    memmove(&user_t[i], &user_t[i + 1], sizeof(Template) * (size_t)(user_n - i - 1));
    user_n--;
}

// Makes room for one more of ch: past HWR_USER_PER_CHAR its oldest goes,
// and when the store is full the oldest sample of all.
static void user_room(char ch)
{
    if (hwr_user_count(ch) >= HWR_USER_PER_CHAR)
    {
        for (int i = 0; i < user_n; i++)
            if (user_t[i].ch == ch)
            {
                user_remove(i);
                break;
            }
    }
    if (user_n >= HWR_USER_MAX)
        user_remove(0);
}

bool hwr_user_add(char ch, const InkPt *pts, int n)
{
    int set = hwr_set_of(ch);
    if (set < 0 || n < 1 || is_dot(pts, n))
        return false;
    if (!ready)
        hwr_init();
    user_room(ch);
    Template *t = &user_t[user_n++];
    int rx[N], ry[N];
    resample(pts, n, rx, ry);
    normalize(rx, ry, t->x, t->y);
    t->ch = ch;
    t->set = (unsigned char)set;
    user_changed = true;
    return true;
}

void hwr_user_forget(char ch)
{
    for (int i = user_n - 1; i >= 0; i--)
        if (ch == 0 || user_t[i].ch == ch)
            user_remove(i);
    user_changed = true;
}

void hwr_user_drop_last(void)
{
    if (user_n > 0)
    {
        user_n--;
        user_changed = true;
    }
}

static void parse_user_line(char *line, void *ctx)
{
    (void)ctx;
    char *s = text_trim(line);
    if (s[0] == '\0' || s[0] == '#' || s[1] != '|' || user_n >= HWR_USER_MAX)
        return;
    char ch = s[0];
    int set = hwr_set_of(ch);
    if (set < 0)
        return;
    Template t;
    t.ch = ch;
    t.set = (unsigned char)set;
    const char *p = s + 2;
    for (int k = 0; k < N; k++)
    {
        char *end;
        long x = strtol(p, &end, 10);
        if (end == p || *end != ',')
            return;
        p = end + 1;
        long y = strtol(p, &end, 10);
        if (end == p || x < -2 * SCALE || x > 2 * SCALE || y < -2 * SCALE || y > 2 * SCALE)
            return;
        t.x[k] = (short)x;
        t.y[k] = (short)y;
        p = end;
    }
    while (*p == ' ')
        p++;
    if (*p != '\0')
        return;   // more points than N: another build's samples
    user_room(ch);
    user_t[user_n++] = t;
}

void hwr_user_parse(char *text)
{
    user_n = 0;
    text_each_line(text, parse_user_line, NULL);
    user_changed = false;
}

bool hwr_user_serialize(TextBuf *b)
{
    textbuf_append(b, "# Handwriting samples: character|32 points x,y (normalized). "
                      "Delete this file to forget them.\n");
    for (int i = 0; i < user_n; i++)
    {
        const Template *t = &user_t[i];
        textbuf_appendf(b, "%c|", t->ch);
        for (int k = 0; k < N; k++)
            textbuf_appendf(b, "%s%d,%d", k ? " " : "", t->x[k], t->y[k]);
        textbuf_append(b, "\n");
    }
    if (!b->failed)
        user_changed = false;
    return !b->failed;
}

// ---------------------------------------------------------------- public

char hwr_gesture(const InkPt *pts, int n, int pad_width)
{
    if (n < 2 || pts[n - 1].stroke != 0)
        return 0;   // More than one stroke.
    int w, h;
    bbox(pts, n, &w, &h);
    if (w < pad_width * 45 / 100 || h * 4 > w)
        return 0;
    return pts[n - 1].x > pts[0].x ? ' ' : '\b';
}

// Full matches run on at most this many templates: the ones a single
// greedy pass (start 0, both ways) finds closest. Below it (the built-in
// shapes alone are fewer), every template gets the full match, exactly as
// before there were user samples.
#define FULL_MAX 64

typedef struct {
    const Template *t;
    uint32_t score;
    bool user;
} Cand;

int hwr_recognize(const InkPt *pts, int n, HwrSet set, char *out, int max)
{
    if (n < 1 || max < 1)
        return 0;
    if (!ready)
        hwr_init();

    if (is_dot(pts, n))
    {
        out[0] = '.';
        return 1;
    }

    int rx[N], ry[N];
    short cx[N], cy[N];
    resample(pts, n, rx, ry);
    normalize(rx, ry, cx, cy);

    // Every template of the set: built-in, then the user's.
    static Cand cand[TEMPLATE_COUNT + HWR_USER_MAX];
    int nc = 0;
    for (int t = 0; t < TEMPLATE_COUNT; t++)
        if (templates[t].set == set)
            cand[nc++] = (Cand){ &templates[t], 0, false };
    for (int t = 0; t < user_n; t++)
        if (user_t[t].set == set)
            cand[nc++] = (Cand){ &user_t[t], 0, true };

    // Too many for a full match each: keep the FULL_MAX closest by one pass.
    if (nc > FULL_MAX)
    {
        for (int i = 0; i < nc; i++)
        {
            uint32_t a = cloud_distance(cx, cy, cand[i].t->x, cand[i].t->y, 0);
            uint32_t b = cloud_distance(cand[i].t->x, cand[i].t->y, cx, cy, 0);
            cand[i].score = a < b ? a : b;
        }
        for (int i = 0; i < FULL_MAX; i++)   // selection of the best FULL_MAX
        {
            int bi = i;
            for (int k = i + 1; k < nc; k++)
                if (cand[k].score < cand[bi].score)
                    bi = k;
            Cand c = cand[i];
            cand[i] = cand[bi];
            cand[bi] = c;
        }
        nc = FULL_MAX;
    }

    // Best full score per character. The user's own samples count 1/8
    // closer: their way of writing a letter wins a near tie.
    char chars[TEMPLATE_COUNT + HWR_USER_MAX];
    uint32_t score[TEMPLATE_COUNT + HWR_USER_MAX];
    int nchars = 0;
    for (int i = 0; i < nc; i++)
    {
        uint32_t d = match(cx, cy, cand[i].t);
        if (cand[i].user)
            d -= d / 8;
        char ch = cand[i].t->ch;
        int k = 0;
        while (k < nchars && chars[k] != ch)
            k++;
        if (k == nchars)
        {
            chars[nchars] = ch;
            score[nchars++] = d;
        }
        else if (d < score[k])
        {
            score[k] = d;
        }
    }

    // Selection sort of the top `max`.
    int got = 0;
    for (; got < max && got < nchars; got++)
    {
        int b = got;
        for (int k = got + 1; k < nchars; k++)
            if (score[k] < score[b])
                b = k;
        char tc = chars[got];
        uint32_t ts = score[got];
        chars[got] = chars[b];
        score[got] = score[b];
        chars[b] = tc;
        score[b] = ts;
        out[got] = chars[got];
    }
    return got;
}
