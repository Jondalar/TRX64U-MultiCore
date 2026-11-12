#include "../components/size_str.h"

#include <stdio.h>
#include <string.h>

void size_to_string_bytes(uint64_t size, char *buffer)
{
    if(size < 1000) {
        sprintf(buffer, "%4d ", (int)size);
        return;
    }
    size += 512;
    size >>= 10;

    if(size < 10000) {
        sprintf(buffer, "%4dK", (int)size);
        return;
    }
    size += 512;
    size >>= 10;
    if(size < 10000) {
        sprintf(buffer, "%4dM", (int)size);
        return;
    }
    size += 512;
    size >>= 10;
    sprintf(buffer, "%4dG", (int)size);
}

void size_to_string_sectors(uint32_t size, char *buffer)
{
    size++;
    size >>= 1;
    
    if(size < 10000) {
        sprintf(buffer, "%4d KB", size);
        return;
    }
    size += 512;
    size >>= 10;

    if(size < 10000) {
        sprintf(buffer, "%4d MB", size);
        return;
    }
    size += 512;
    size >>= 10;
    sprintf(buffer, "%4d GB", size);
}
