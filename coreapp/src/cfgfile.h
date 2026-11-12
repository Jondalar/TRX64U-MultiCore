// The text format of <name>.cfg (T93 D10): Gideon's .cfg format, so his tools read the file and the keys keep their
// meaning - "[Store name]" on a line, then "Item text=Value" lines (an enum item's value is its label, a number its
// decimal digits). This is the part of vendor/gideon/software/userinterface/configio.cc that a core application needs, written
// for this code (own parser, no Config* classes). Pure.
#ifndef COREAPP_CFGFILE_H
#define COREAPP_CFGFILE_H

// Calls item(user, store, key, value) for every "key=value" line inside a "[store]" section; comment lines (# ;), blank
// lines and lines outside a section are skipped. Spaces around the key and the value are ignored, CR is ignored. Lines longer than
// 127 characters are skipped as a whole (they are not valid items). Returns the number of items passed.
typedef void (*cfg_item_cb)(void *user, const char *store, const char *key, const char *value);
int cfgfile_parse(const char *text, int len, cfg_item_cb item, void *user);

// Appends "[store]\n" to out[*pos] (never past max); false when it does not fit. A section is closed with a blank line.
bool cfgfile_begin(char *out, int max, int *pos, const char *store);
bool cfgfile_item(char *out, int max, int *pos, const char *key, const char *value);
bool cfgfile_end(char *out, int max, int *pos);

// Gideon's comparison of an enum label: case-insensitive, outer spaces ignored (" ~45" equals "~45").
bool cfgfile_label_equal(const char *a, const char *b);
// The same after a trailing note in brackets is cut from each ("Auto (1280 x 720 from host)" is "Auto"): a label that carries a note which changes with the
// machine (the Picture item's Auto, T164 D10c) is still found by the label a .cfg, a web page or an older release stored. A second pass after the exact one.
bool cfgfile_label_head_equal(const char *a, const char *b);

#endif
