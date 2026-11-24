// The Amiga 1200's own app (cores/amiga1200/app) has no amiga_regs.h: its registers are amiga1200_regs.h, named through its driver table. The test files that serve both the shared app and the
// A1200's app include "amiga_regs.h"; in a build as the A1200's app (run.sh a12app, TEST_APP_A1200) this file stands first on the include path and gives them the A1200's header, with the same AM_* names.
#include "amiga1200_regs.h"
