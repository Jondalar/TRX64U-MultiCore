// Host test of the host app's zone table (T164 D1, DST rules): every zone's POSIX string gives the offsets of the real zone,
// at fixed instants around the EU, US and southern-hemisphere changes. Built against the table given as TZ_TABLE.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include TZ_TABLE

static int fails;

// offset of local time from UTC in minutes for zone `loc` at the UTC instant y-M-D h:00
static int offset(const char *loc, int y, int M, int D, int h)
{
    const char *posix = 0;
    for (unsigned i = 0; i < sizeof(zones) / sizeof(zones[0]); i++)
        if (!strcmp(zones[i].location, loc)) posix = zones[i].posix;
    if (!posix) { printf("FAIL no zone %s\n", loc); fails++; return -9999; }
    setenv("TZ", posix, 1);
    tzset();
    struct tm u = {};
    u.tm_year = y - 1900; u.tm_mon = M - 1; u.tm_mday = D; u.tm_hour = h;
    time_t t = timegm(&u);
    struct tm l;
    localtime_r(&t, &l);
    return (int)(l.tm_gmtoff / 60);
}

#define AT(loc, y, M, D, h, mins) do { int o = offset(loc, y, M, D, h); if (o != (mins)) { \
    printf("FAIL %s %04d-%02d-%02d %02d:00Z: %+d min, expected %+d\n", loc, y, M, D, h, o, (mins)); fails++; } } while (0)

int main()
{
    // EU rule: last Sunday of March 01:00 UTC to last Sunday of October 01:00 UTC (2026: 29 March, 25 October)
    AT("Western Europe", 2026, 3, 28, 12, 60);  AT("Western Europe", 2026, 3, 29, 12, 120);
    AT("Western Europe", 2026, 10, 24, 12, 120); AT("Western Europe", 2026, 10, 25, 12, 60);
    AT("Western Europe", 2026, 3, 29, 0, 60);   AT("Western Europe", 2026, 3, 29, 1, 120);
    AT("Western Europe", 2026, 10, 25, 0, 120); AT("Western Europe", 2026, 10, 25, 1, 60);
    AT("UK", 2026, 1, 15, 12, 0);               AT("UK", 2026, 3, 29, 12, 60);  AT("UK", 2026, 10, 25, 12, 0);
    AT("Greece", 2026, 1, 15, 12, 120);         AT("Greece", 2026, 7, 15, 12, 180); AT("Greece", 2026, 10, 25, 12, 120);
    AT("Greenland", 2026, 1, 15, 12, -120);     AT("Greenland", 2026, 7, 15, 12, -60);
    // US rule: second Sunday of March to first Sunday of November, 02:00 local (2026: 8 March, 1 November)
    AT("New York", 2026, 3, 7, 12, -300);       AT("New York", 2026, 3, 8, 12, -240);
    AT("New York", 2026, 10, 31, 12, -240);     AT("New York", 2026, 11, 1, 12, -300);
    AT("Los Angeles", 2026, 7, 15, 12, -420);   AT("Denver, Colorado", 2026, 1, 15, 12, -420);
    AT("Chicago, Illinois", 2026, 7, 15, 12, -300); AT("Alaska", 2026, 7, 15, 12, -480);
    AT("Newfoundland", 2026, 1, 15, 12, -210);  AT("Newfoundland", 2026, 7, 15, 12, -150);
    AT("Phoenix, Arizona", 2026, 7, 15, 12, -420);
    AT("Hawaii", 2026, 1, 15, 12, -600);        AT("Hawaii", 2026, 7, 15, 12, -600);
    AT("French Polynesia", 2026, 7, 15, 12, -570);
    // southern hemisphere: summer time from the first Sunday of October (NZ: last Sunday of September) to the first Sunday of April
    AT("Eastern Australia", 2026, 1, 15, 12, 660); AT("Eastern Australia", 2026, 7, 15, 12, 600);
    AT("Eastern Australia", 2026, 4, 5, 12, 600);  AT("Eastern Australia", 2026, 10, 4, 12, 660);
    AT("Central Australia", 2026, 1, 15, 12, 630); AT("Central Australia", 2026, 7, 15, 12, 570);
    AT("Lord Howe Island", 2026, 1, 15, 12, 660);  AT("Lord Howe Island", 2026, 7, 15, 12, 630);
    AT("New Zealand", 2026, 1, 15, 12, 780);    AT("New Zealand", 2026, 7, 15, 12, 720); AT("New Zealand", 2026, 9, 27, 12, 780);
    AT("Chatham Islands", 2026, 1, 15, 12, 825); AT("Chatham Islands", 2026, 7, 15, 12, 765);
    // no summer time (any more)
    AT("Iran", 2026, 7, 15, 12, 210);           AT("Tonga", 2026, 1, 15, 12, 780); AT("Tonga", 2026, 7, 15, 12, 780);
    AT("Japan", 2026, 7, 15, 12, 540);          AT("India", 2026, 7, 15, 12, 330); AT("Nepal", 2026, 7, 15, 12, 345);

    printf(fails ? "test_tz: %d FAILED\n" : "test_tz: all passed\n", fails);
    return fails ? 1 : 0;
}
