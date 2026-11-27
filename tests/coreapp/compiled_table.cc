// The application's real core table under another name, for tests that need the compiled-in descriptors next to a table of their own (test_descgen: the record's core is
// compared with the compiled-in descriptor of the same core, while the start must not find that one).
#error "compiled_table.cc is the compiled-in table of a core's app: build it as that app (run.sh <core>app, TEST_APP_<CORE>); the shared table is empty (T239.3 D61)"
