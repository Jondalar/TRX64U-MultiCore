// The applications of the core folder add nothing to a native menu: appindex.cc reads .u2a records through the library loader (tests/loader covers it) and is linked
// for real only where the whole application is (the render bench).
#include "appindex.h"
int AppIndex::contributors(Contributor *, int) const { return 0; }
