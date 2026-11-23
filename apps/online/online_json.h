// The JSON reader of the online sub-app: what the search server answers (a list of presets, a list of results, an object of entries) is read into a tree of nodes in one block.
// Ours (the firmware's json.h is a C++ class tree with new and delete, which an application image does not have). A text is read in place: the strings of the tree are
// decoded into the text's own bytes, so the text is the tree's memory too and must outlive it.
#ifndef APPS_ONLINE_JSON_H
#define APPS_ONLINE_JSON_H

#include <stdint.h>

enum { J_NULL = 0, J_BOOL = 1, J_INT = 2, J_STR = 3, J_LIST = 4, J_OBJ = 5 };

struct JNode {
    uint8_t type;
    int32_t num;                 // J_INT: the integer part of the number; J_BOOL: 0 or 1
    const char *str;             // J_STR: the text, NUL terminated, in the source text
    const char *key;             // the member name when the node is a member of an object, else null
    JNode *child;                // J_LIST, J_OBJ: the first member
    JNode *next;                 // the next node of the same list or object
};

struct JTree {
    JNode *root;                 // null: the text is no JSON
    JNode *pool;                 // the one block of the nodes (the caller frees it with the allocator it gave)
    int nodes;
};

// Reads `text` (len bytes, modified in place; one more byte after it is not needed). `alloc` gives the block of the nodes; null: no room, root stays null.
JTree json_parse(char *text, int len, void *(*alloc)(uint32_t n, void *ctx), void *ctx);

const JNode *json_member(const JNode *obj, const char *key);                   // the member of an object, null when none or not an object
const char *json_text(const JNode *obj, const char *key);                      // the member as text; "" when absent or not a string
int json_int(const JNode *obj, const char *key, int def);                      // the member as integer; def when absent or not a number

#endif
