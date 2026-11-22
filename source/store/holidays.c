#include "holidays.h"

// How a holiday's date is found.
enum { FIXED, NTH, LAST, EASTER };

typedef struct {
    int rule;
    int month;
    int a;        // FIXED: the day. NTH: which one (1..5). EASTER: days after.
    int weekday;  // NTH, LAST: 0 = Sunday .. 6 = Saturday
    const char *name;
} Rule;

// In calendar order, so a day with two lists them in a sensible order.
static const Rule RULES[] = {
    { FIXED, 1, 1, 0, "New Year's Day" },
    { NTH, 1, 3, 1, "MLK Day" },
    { FIXED, 2, 14, 0, "Valentine's Day" },
    { NTH, 2, 3, 1, "Presidents' Day" },
    { NTH, 3, 2, 0, "Clocks forward 1h" },
    { FIXED, 3, 17, 0, "St. Patrick's Day" },
    { EASTER, 0, 0, 0, "Easter" },
    { NTH, 5, 2, 0, "Mother's Day" },
    { LAST, 5, 0, 1, "Memorial Day" },
    { NTH, 6, 3, 0, "Father's Day" },
    { FIXED, 6, 19, 0, "Juneteenth" },
    { FIXED, 7, 4, 0, "Independence Day" },
    { NTH, 9, 1, 1, "Labor Day" },
    { NTH, 10, 2, 1, "Columbus Day" },
    { FIXED, 10, 31, 0, "Halloween" },
    { NTH, 11, 1, 0, "Clocks back 1h" },
    { FIXED, 11, 11, 0, "Veterans Day" },
    { NTH, 11, 4, 4, "Thanksgiving" },
    { FIXED, 12, 24, 0, "Christmas Eve" },
    { FIXED, 12, 25, 0, "Christmas" },
    { FIXED, 12, 31, 0, "New Year's Eve" },
};
#define RULE_COUNT ((int)(sizeof(RULES) / sizeof(RULES[0])))

Date holiday_easter(int y)
{
    // The anonymous Gregorian algorithm (Meeus/Jones/Butcher).
    int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4;
    int f = (b + 8) / 25, g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4, k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    int month = (h + l - 7 * m + 114) / 31;
    int day = (h + l - 7 * m + 114) % 31 + 1;
    Date out = { y, month, day };
    return out;
}

// The date of rule r in year y.
static Date rule_date(const Rule *r, int y)
{
    Date d = { y, r->month, 1 };
    switch (r->rule)
    {
        case FIXED:
            d.d = r->a;
            return d;
        case NTH:
        {
            int first = date_weekday(d);
            d.d = 1 + (r->weekday - first + 7) % 7 + 7 * (r->a - 1);
            return d;
        }
        case LAST:
        {
            d.d = date_days_in_month(y, r->month);
            int last = date_weekday(d);
            d.d -= (last - r->weekday + 7) % 7;
            return d;
        }
        default:
            return date_add_days(holiday_easter(y), r->a);
    }
}

int holidays_on(Date day, const char **out, int max)
{
    int n = 0;
    for (int i = 0; i < RULE_COUNT && n < max; i++)
    {
        const Rule *r = &RULES[i];
        if (r->rule != EASTER && r->month != day.m)
            continue;
        if (date_cmp(rule_date(r, day.y), day) == 0)
            out[n++] = r->name;
    }
    return n;
}

bool holiday_next(Date from, int days, Date *when, const char **name)
{
    for (int k = 0; k < days; k++)
    {
        Date d = date_add_days(from, k);
        if (holidays_on(d, name, 1) > 0)
        {
            *when = d;
            return true;
        }
    }
    return false;
}
