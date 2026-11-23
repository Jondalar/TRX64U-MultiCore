// The application's real core table under another name, for tests that need the compiled-in descriptors next to a table of their own (test_descgen: the record's core is
// compared with the compiled-in descriptor of the same core, while the start must not find that one).
#define core_table compiled_in_table
#include "table.cc"
