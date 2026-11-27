// The application's real core table under another name, for tests that need the compiled-in descriptors next to a table of their own (test_descgen: the record's core is
// compared with the compiled-in descriptor of the same core, while the start must not find that one).
// the Neo Geo's app is its own build (run.sh neogeoapp): its table is its core only
#include "cores.h"
extern const CoreDesc neogeo_desc; // cores.h lists the shared app's cores; the Neo Geo is its own app (cores/neogeo/app/lib)
extern const CoreDesc *const compiled_in_table[];
const CoreDesc *const compiled_in_table[] = { &neogeo_desc, 0 };
