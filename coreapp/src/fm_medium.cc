#include "fm_fs.h"
#include "fsif.h"
#include "file_device.h"
#include "mediasniff.h"
#include "mediaui.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

// The media level of the Library: what the medium is, as the host's File Browser says it (T111 words: size and file system). The sectors that tell are read once per
// medium and ten seconds; a medium that comes or goes is seen at once (ready, sector count).
bool fm_medium_info(const char *name, MediumInfo *out)
{
    FileDevice *d = FileDevice::find(name);
    if (!d) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    BlockDevice *b = d->get_block_device();
    out->ready = d->is_ready() && !(b->status() & (STA_NODISK | STA_NOINIT));
    if (!out->ready) {
        snprintf(out->contents, sizeof(out->contents), "no medium");
        return true;
    }
    char probe[40];
    snprintf(probe, sizeof(probe), "/%s/.", name);                 // the flash medium is as writable as CoreFs lets a write through (fsif.h: fs_write_refused), not what its block device reports
    out->writable = !(b->status() & STA_PROTECT) && !fs_write_refused(probe);
    uint32_t ss = 512, cnt = 0;
    b->ioctl(GET_SECTOR_SIZE, &ss);
    if (b->ioctl(GET_SECTOR_COUNT, &cnt) != RES_OK) {
        cnt = 0;
    }
    struct Cache { char name[24]; uint32_t sectors, at; MediumInfo mi; };
    static Cache cache[6];
    uint32_t now = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    Cache *c = 0, *free_slot = 0;
    for (int i = 0; i < 6; i++) {
        if (!strcmp(cache[i].name, name)) {
            c = &cache[i];
        } else if (!cache[i].name[0] && !free_slot) {
            free_slot = &cache[i];
        }
    }
    if (c && c->sectors == cnt && now - c->at < 10000) {
        *out = c->mi;
        return true;
    }
    if (!c) {
        c = free_slot ? free_slot : &cache[0];
    }
    MediaSniff sn;
    media_sniff([](void *ctx, uint32_t lba, uint8_t *buf) { return ((BlockDevice *)ctx)->read(buf, lba, 1) == RES_OK; }, b, cnt, ss, &sn);
    ui_size_text(out->size, sizeof(out->size), (uint64_t)cnt * ss);
    ui_fs_text(out->contents, sizeof(out->contents), &sn);
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->sectors = cnt;
    c->at = now;
    c->mi = *out;
    return true;
}
