#ifndef _C64_CRT_H
#define _C64_CRT_H

#include "filemanager.h"
#include "c64.h"
#include "subsys.h" // also FreeRTOS and its semaphores

#define CARTS_DIRECTORY "/flash/carts"

// Local definitions, NOT hardware select!
typedef enum {
    CART_NOT_IMPL,
    CART_NORMAL,
    CART_ACTION,
    CART_RETRO,
    CART_DOMARK,
    CART_OCEAN_8K,
    CART_OCEAN_16K,
    CART_EASYFLASH,
    CART_SUPERSNAP,
    CART_EPYX,
    CART_FINAL3,
    CART_SYSTEM3,
    CART_ZAXXON,
    CART_KCS,
    CART_FINAL12,
    CART_COMAL80,
    CART_SBASIC,
    CART_WESTERMANN,
    CART_BBASIC,
    CART_PAGEFOX,
    CART_EXOS,
    CART_SUPERGAMES,
    CART_NORDIC,
    CART_GMOD2,
    CART_BLACKBOX3,
    CART_BLACKBOX4,
    CART_BLACKBOX8,
    CART_BLACKBOX9,
    CART_MEGABYTER,
    CART_TWOMEGABYTER,
    CART_C128_STD,
    CART_C128_STD_IO,
    CART_C64MEGACART,   // TRX64 bitstream only; Gideon's: loaded as Ocean 8K
    CART_DINAMIC,       // TRX64 bitstream only
    CART_MAGICDESK16,   // TRX64 bitstream only
    CART_EASYFLASH_XL,  // TRX64 bitstream only
    CART_EASYFLASH3,    // TRX64 bitstream with the EasyFlash 3 block only (T51): CRT type 90, 8 MiB
} e_known_cart;

class C64_CRT
{
    struct t_cart {
        uint16_t     crt_type;
        uint8_t      max_bank;
        e_known_cart local_type;
        const char  *cart_name;
    };

    struct t_crt_chip_chunk
    {
        uint8_t header[0x10];
        uint8_t *ram_location;
        bool mandatory; // if false, a chunk that is all $FF is left out of a saved file
        bool last;
    };

    const static struct t_cart c_recognized_c64_carts[];
    const static struct t_cart c_recognized_c128_carts[];

    // Local Variables
    uint8_t     *cart_memory;
    uint32_t     max_cart;
    int          bank_multiplier;
    int          machine; // 64 or 128
    bool         a000_seen;
    e_known_cart local_type;
    uint8_t      max_bank;
    uint8_t      highest_bank;
    uint32_t     total_read;

    // CRT File Structure
    uint8_t      crt_header[0x40]; // includes name field
    IndexedList<t_crt_chip_chunk *>chip_chunks;

    // Auxiliary Data chunks
    uint8_t     *eeprom_buffer;
    int          eeprom_size;

    // Full pathname the image was loaded from or last saved to; empty if it came from no file.
    mstring      source;

    // Hash of the image as loaded or last saved; a different hash means the C64 changed it.
    uint32_t     baseline_hash;

    // Copy of what the C64 can change, taken while it runs, and the hash of that copy.
    uint8_t     *snapshot;
    uint32_t     snapshot_hash;

    // Set by a change the firmware carries out itself: an EasyFlash sector erase over UCI.
    volatile bool change_notice;

    // Loads come from the user interface and REST, saves also from the auto-save task.
    SemaphoreHandle_t mutex;

    static C64_CRT *get_instance(void); // singleton

    C64_CRT();
    void initialize(uint8_t *mem, uint32_t max_size);
    void cleanup(void);

    SubsysResultCode_e check_header(File *f, cart_def *def);
    SubsysResultCode_e read_chip_packet(File *f, t_crt_chip_chunk *chunk);
    void clear_cart_mem(void);
    void regenerate_flash_chunks();
    SubsysResultCode_e read_crt(File *file, cart_def *def);
    void configure_cart(cart_def *def);
    void find_eeprom(void);
    void auto_mirror(void);
    uint32_t content_hash(const uint8_t *snap);
    void pull_eeprom(uint8_t *dst);
    SubsysResultCode_e write_image(File *fo, const uint8_t *snap);
    void drop_snapshot(void);
    t_crt_chip_chunk *eeprom_chunk(void);
public:
    static SubsysResultCode_e load_crt(const char *path, const char *filename, cart_def *def, uint8_t *mem);
    static SubsysResultCode_e save_crt(File *f);
    static int clear_crt(void);
    static bool is_valid(void);
    static void set_source(const char *path, const char *filename);
    static const char *get_source(void);

    // Change detection. The hash covers what save_crt() would write; the baseline is the state of
    // the file, set when the image is loaded and by every successful save.
    static uint32_t current_hash(void);
    static uint32_t get_baseline(void);
    static void set_baseline(uint32_t hash);

    // Whether the C64 wrote since the last call, as far as the firmware can see it: the
    // GMod2 EEPROM's dirty flag, whose contents are taken into the image on the way, or
    // an EasyFlash sector erase over UCI. EasyFlash bytes programmed through the $DE09
    // write path pass unseen; the hash still finds them.
    static void note_change(void);
    static bool take_change_notice(void);

    // Saving while the C64 runs: copy what it can change, then write the file from the
    // copy. False if the image changed during the copy or there is no memory for it.
    static bool take_snapshot(uint32_t *hash);
    static SubsysResultCode_e save_snapshot(File *f);
    static void discard_snapshot(void);

    static void clear_definition(cart_def *def);
};

#endif
