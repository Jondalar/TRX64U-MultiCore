// The bug report text (T162): one plain-text file that says which software was running, how it was set up and what it had just printed.
// Pure code shared by the host app (hostreport.cc) and the generic core application (coreapp/src/corereport.cc); tests/hostapp and
// tests/coreapp run it natively. Nothing in here reads a device: the callers hand in the facts.
//
// P4 (privacy): a report holds no network credentials and no file contents beyond hashes. The rule is enforced here, in one place:
//   - br_secret_name(): a setting or key whose NAME says it is a secret has its value replaced by "<withheld>" (br_kv, br_text);
//   - br_url_scrub(): the user:password@ part of a URL is cut wherever a value or a log line holds one;
//   - br_log(): a log line that holds a credential word is replaced by a marker line, never copied.
#ifndef HOST_BUGREPORT_H
#define HOST_BUGREPORT_H

#include <stdint.h>

#define BR_LOG_LINES 200            // T162 D2: the last 200 log lines ...
#define BR_LOG_BYTES 16384          // ... or 16 KB of log, whichever is smaller
#define BR_STAMP_LEN 16             // "YYYYMMDD-HHMMSS" and the terminator
#define BR_WITHHELD  "<withheld>"

struct BrOut {
    char *buf;
    int cap;
    int n;
    bool cut;                       // the text did not fit: br_finish() puts a marker at its end
};

void br_init(BrOut *o, char *buf, int cap);
void br_put(BrOut *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void br_section(BrOut *o, const char *title);            // a blank line, then "== title =="
// "key: value". A secret key (br_secret_name) writes BR_WITHHELD instead of the value; a URL value loses its user:password@.
void br_kv(BrOut *o, const char *key, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
// A settings text as the stores keep it ("[section]" lines and "key=value" lines): copied line by line, a secret key's value withheld, URLs scrubbed.
void br_text(BrOut *o, const char *text, int len);
// The tail of a log: at most BR_LOG_LINES lines and BR_LOG_BYTES bytes, in order, each line scrubbed; a line with a credential word becomes a marker.
void br_log(BrOut *o, const char *log, int len);
void br_finish(BrOut *o);                                  // terminates; marks a cut report

bool br_secret_name(const char *name);                    // password, passphrase, psk, secret, token, ... or a name ending in "key"
bool br_secret_word(const char *line);                    // a log line that must not be copied
int  br_url_scrub(const char *in, char *out, int max);    // "ftp://u:p@h/x" -> "ftp://***@h/x"; other text unchanged; returns the length

// The FAT time the clock gives (get_fattime) as "YYYYMMDD-HHMMSS"; valid false (clock never set) gives "nodate" (the caller adds -<n>).
void br_stamp(char *out, uint32_t fattime, bool valid);
void br_date(char *out, int max, uint32_t fattime, bool valid);   // "2026-10-30 14:05:09" or "clock not set"

#endif
