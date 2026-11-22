#include "date.h"

#include <stdio.h>
#include <string.h>

const char *const DATE_DAY[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
                                  "Friday", "Saturday" };
const char *const DATE_DAY3[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char *const DATE_MONTH[12] = { "January", "February", "March", "April", "May", "June",
                                     "July", "August", "September", "October", "November",
                                     "December" };
const char *const DATE_MONTH3[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
const char *const DATE_GRID_DAY[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };

bool date_is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int date_days_in_month(int y, int m)
{
    static const int dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12)
        return 0;
    if (m == 2 && date_is_leap(y))
        return 29;
    return dim[m - 1];
}

bool date_valid(Date dt)
{
    return dt.y >= 1900 && dt.y <= 2999 && dt.m >= 1 && dt.m <= 12 &&
           dt.d >= 1 && dt.d <= date_days_in_month(dt.y, dt.m);
}

// Howard Hinnant's days_from_civil.
long date_to_days(Date dt)
{
    int y = dt.y - (dt.m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned mp = (unsigned)(dt.m + (dt.m > 2 ? -3 : 9));
    unsigned doy = (153 * mp + 2) / 5 + (unsigned)dt.d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

Date date_from_days(long z)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    Date out = { (int)(y + (m <= 2)), (int)m, (int)d };
    return out;
}

Date date_add_days(Date dt, long n)
{
    return date_from_days(date_to_days(dt) + n);
}

Date date_add_months(Date dt, int n)
{
    int m = dt.m - 1 + n;
    int y = dt.y + (m < 0 ? (m - 11) / 12 : m / 12);
    m = ((m % 12) + 12) % 12 + 1;
    int dim = date_days_in_month(y, m);
    Date out = { y, m, dt.d > dim ? dim : dt.d };
    return out;
}

int date_weekday(Date dt)
{
    long days = date_to_days(dt);
    // 1970-01-01 was a Thursday (4).
    long w = (days + 4) % 7;
    if (w < 0)
        w += 7;
    return (int)w;
}

int date_cmp(Date a, Date b)
{
    if (a.y != b.y)
        return a.y < b.y ? -1 : 1;
    if (a.m != b.m)
        return a.m < b.m ? -1 : 1;
    if (a.d != b.d)
        return a.d < b.d ? -1 : 1;
    return 0;
}

static int digits(const char *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++)
    {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

static bool has_len(const char *s, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (s[i] == '\0')
            return false;
    }
    return true;
}

bool date_parse(const char *s, Date *out)
{
    if (!has_len(s, 10) || s[4] != '-' || s[7] != '-')
        return false;
    Date dt = { digits(s, 4), digits(s + 5, 2), digits(s + 8, 2) };
    if (dt.y < 0 || dt.m < 0 || dt.d < 0 || !date_valid(dt))
        return false;
    *out = dt;
    return true;
}

void date_format(Date dt, char out[11])
{
    snprintf(out, 11, "%04u-%02u-%02u", (unsigned)dt.y % 10000u, (unsigned)dt.m % 100u,
             (unsigned)dt.d % 100u);
}

// Lowercase copy of s into out (at most n-1 chars). Returns false when too long.
static bool lower_copy(const char *s, char *out, int n)
{
    int i = 0;
    for (; s[i] != '\0'; i++)
    {
        if (i >= n - 1)
            return false;
        char c = s[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    out[i] = '\0';
    return true;
}

// True when w is at least 3 characters and a prefix of full.
static bool word_is(const char *w, const char *full)
{
    int n = 0;
    while (w[n] != '\0')
    {
        if (full[n] == '\0' || full[n] != w[n])
            return false;
        n++;
    }
    return n >= 3;
}

// Parses 1 or 2 digits from *s and advances it. Returns -1 on error.
static int small_number(const char **s)
{
    int v = 0, n = 0;
    while (**s >= '0' && **s <= '9' && n < 2)
    {
        v = v * 10 + (**s - '0');
        (*s)++;
        n++;
    }
    return n == 0 ? -1 : v;
}

bool date_parse_relative(const char *s, Date today, Date *out)
{
    static const char *const WEEKDAYS[7] = { "sunday", "monday", "tuesday", "wednesday",
                                             "thursday", "friday", "saturday" };
    char w[16];
    if (!lower_copy(s, w, sizeof(w)) || w[0] == '\0')
        return false;

    if (date_parse(w, out))
        return true;
    if (word_is(w, "today"))
    {
        *out = today;
        return true;
    }
    if (strcmp(w, "tmr") == 0 || strcmp(w, "tmrw") == 0 || word_is(w, "tomorrow"))
    {
        *out = date_add_days(today, 1);
        return true;
    }
    for (int wd = 0; wd < 7; wd++)
    {
        if (word_is(w, WEEKDAYS[wd]))
        {
            int ahead = (wd - date_weekday(today) + 7) % 7;
            *out = date_add_days(today, ahead == 0 ? 7 : ahead);
            return true;
        }
    }
    if (w[0] == '+')
    {
        const char *p = w + 1;
        long n = 0;
        int digits_seen = 0;
        while (*p >= '0' && *p <= '9' && digits_seen < 3)
        {
            n = n * 10 + (*p - '0');
            p++;
            digits_seen++;
        }
        if (digits_seen == 0)
            return false;
        if (*p == 'w' && p[1] == '\0')
            n *= 7;
        else if (!(*p == '\0' || (*p == 'd' && p[1] == '\0')))
            return false;
        *out = date_add_days(today, n);
        return true;
    }
    // MM-DD or M/D.
    const char *p = w;
    int m = small_number(&p);
    if (m < 0 || (*p != '-' && *p != '/'))
        return false;
    p++;
    int d = small_number(&p);
    if (d < 0 || *p != '\0')
        return false;
    // This year, or the next year in which the date exists and is not past
    // (Feb 29 can be up to 8 years away).
    for (int y = today.y; y <= today.y + 8; y++)
    {
        Date c = { y, m, d };
        if (date_valid(c) && date_cmp(c, today) >= 0)
        {
            *out = c;
            return true;
        }
    }
    return false;
}

void time_fmt12(int minute, int style, char *out, int size)
{
    minute = ((minute % 1440) + 1440) % 1440;
    int h = minute / 60, m = minute % 60;
    bool pm = h >= 12;
    int h12 = h % 12 == 0 ? 12 : h % 12;
    if (style == TIME_LONG)
        snprintf(out, (size_t)size, "%d:%02d %s", h12, m, pm ? "PM" : "AM");
    else if (style == TIME_SHORT && m == 0)
        snprintf(out, (size_t)size, "%d%c", h12, pm ? 'p' : 'a');
    else
        snprintf(out, (size_t)size, "%d:%02d%c", h12, m, pm ? 'p' : 'a');
}

int time_parse_hhmm(const char *s)
{
    if (!has_len(s, 5) || s[2] != ':')
        return -1;
    int h = digits(s, 2), m = digits(s + 3, 2);
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return -1;
    return h * 60 + m;
}
