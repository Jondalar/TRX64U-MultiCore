// The application's real core table under another name, for tests that need the compiled-in descriptors next to a table of their own (test_descgen: the record's core is
// compared with the compiled-in descriptor of the same core, while the start must not find that one).
// the Commodore 264's app is its own build (run.sh c264app): its table is its core only
#include "cores.h"
extern const CoreDesc c264_desc;   // no shared header declares it (cores/c264/app/lib holds this one)
extern const CoreDesc *const compiled_in_table[];
const CoreDesc *const compiled_in_table[] = { &c264_desc, 0 };
