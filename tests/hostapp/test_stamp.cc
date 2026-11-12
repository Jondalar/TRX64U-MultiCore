// Host test of the legacy stamp text (legacy.cc legacy_stamp_text, extracted by run.sh because legacy.cc needs the board).
// Stamps written before the SHA-checked copy (commit ea00f22d2) are version 1 ("cor|", "cfw|"); the version in the prefix makes them
// never equal to what a start wants, so the next start copies again.
#include "cfw.h"
#include <stdio.h>
#include <string.h>
int legacy_stamp_text(const CfwInfo &ci, uint32_t file_mtime, char *out, int max);
#include STAMP_FN
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main()
{
    CfwInfo ci; memset(&ci, 0, sizeof(ci));
    char t[256];
    ci.from_cor = true; snprintf(ci.id, sizeof(ci.id), "amiga"); ci.bit_len = 1234; snprintf(ci.bit_sha, sizeof(ci.bit_sha), "abcd");
    CHECK(legacy_stamp_text(ci, 0, t, sizeof(t)) > 0 && !strcmp(t, "cor2|amiga|1234|abcd\n"));
    // the stamp file of the old version, byte for byte as it was written, is not what a start wants (stamp_matches compares the text)
    const char *old_cor = "cor|amiga|1234|abcd\n";
    CHECK(strncmp(t, old_cor, strlen(old_cor)) != 0);
    ci.from_cor = false; ci.file_size = 10; ci.bit_off = 20; ci.bit_len = 30;
    CHECK(legacy_stamp_text(ci, 7, t, sizeof(t)) > 0 && !strcmp(t, "cfw2|10|7|20|30\n"));
    const char *old_cfw = "cfw|10|7|20|30\n";
    CHECK(strncmp(t, old_cfw, strlen(old_cfw)) != 0);
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
