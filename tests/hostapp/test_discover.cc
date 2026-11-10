// Host test of the browser's file-name test (discover.cc) and the list window (listwin.h), T101 S3.
#include "discover.h"
#include "listwin.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main()
{
    // T101 S10: the only thing left of the old scan is the name test of the Add core browser
    CHECK(discover_is_bundle("x.ue2") && discover_is_bundle("X.UE2"));                         // T115 D9
    CHECK(discover_is_bundle("a.cor") && discover_is_bundle("A.CFW") && discover_is_bundle("x.Cfw") && !discover_is_bundle(".cfw") &&
          !discover_is_bundle("a.cfwx") && !discover_is_bundle("a.cfw.txt") && !discover_is_bundle("cor") && !discover_is_bundle("a.cob"));

    // S3: the window
    for (int rows = 1; rows <= 5; rows++) {
        for (int total = 0; total <= 12; total++) {
            int first = 0;
            for (int pass = 0; pass < 2; pass++) {          // down, then up
                for (int k = 0; k < total; k++) {
                    int sel = pass == 0 ? k : total - 1 - k;
                    int before = first;
                    bool ch = listwin_fix(sel, total, rows, &first);
                    CHECK(sel >= first && sel < first + rows);
                    CHECK(first >= 0 && first + rows <= (total > rows ? total : rows));
                    CHECK(ch == (first != before));
                    if (before <= sel && sel < before + rows && before + rows <= total) CHECK(!ch);   // never moves a window that holds it
                }
            }
        }
    }
    int first = 0;
    CHECK(!listwin_fix(4, 12, 5, &first) && first == 0);
    CHECK(listwin_fix(5, 12, 5, &first) && first == 1);
    CHECK(!listwin_fix(3, 12, 5, &first) && first == 1);
    CHECK(listwin_fix(0, 12, 5, &first) && first == 0);
    first = 9;                                              // the list got shorter
    CHECK(listwin_fix(2, 6, 5, &first) && first == 1);
    printf(fails ? "test_discover: %d FAILED\n" : "test_discover: ok\n", fails);
    return fails != 0;
}
