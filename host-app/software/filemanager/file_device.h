#ifndef FILE_DEVICE_H
#define FILE_DEVICE_H

#include "blockdev.h"
#include "disk.h"
#include "partition.h"
#include "path.h"
#include "cached_tree_node.h"

class FileDevice : public CachedTreeNode
{
    BlockDevice *blk;
    Disk *disk;
    const char *display_name;
    bool initialized;
    bool isFloppy;
    FileDevice *next_device;       // T111: every FileDevice is in one list, so the format can find a medium by the name the root directory lists
public:
    FileDevice(BlockDevice *b, const char *n, const char *dispn);
    virtual ~FileDevice();
    
    void setFloppy(void) { isFloppy = true; }
    bool is_ready(void);
    void attach_disk(int block_size);
    void detach_disk(void);
    // T111: the format needs the block device itself, the volume released before the disk is detached (detach_disk leaves info.fs, the mounted
    // FileSystemFAT, standing: probe() later overwrites the pointer), and to find a device by name. detach_disk itself is not changed.
    BlockDevice *get_block_device(void) { return blk; }
    void release_volume(void);
    static FileDevice *find(const char *name);
    int  probe(void);
    // int fetch_children(void);
    void get_display_string(char *buffer, int width);
};

#endif
