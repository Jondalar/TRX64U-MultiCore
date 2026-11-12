#include "discover.h"
#include <string.h>
#include <strings.h>

bool discover_is_bundle(const char *name)
{
    int l = strlen(name);
    return l > 4 && (!strcasecmp(name + l - 4, ".cor") || !strcasecmp(name + l - 4, ".cfw") || !strcasecmp(name + l - 4, ".ue2"));
}
