// The built-in drive A as a library (T104.1 D15): a 1541, and where the gateware offers it a 1581, behind the IEC bus of a core.
//
// What it keeps of Gideon's c1541.cc is the drive logic: the register sequences that insert and remove a disk, the parameter RAM that
// points the head at the track images, the dirty flags and what is done about them, the ROM and RAM image of the drive CPU, the
// device number. What it leaves is the structure around it (SubSystem, ConfigurableObject, ObjectWithMenu, the task and the popups):
// the calls here return a sentence instead of asking, and `poll()` is called by whoever owns the schedule (the core's service).
//
// Pure: CoreIo and CoreFs only, so tests/coreapp/ runs it against a model of the drive hardware.
#ifndef CBM1541_DRIVE_H
#define CBM1541_DRIVE_H

#include <stdint.h>
#include "io.h"
#include "fsif.h"
#include "newfile.h"
#include "d1541_gcr.h"
#include "d1541_wd.h"

// DRIVE_A_BASE registers (c1541.h:48)
#define DR_POWER       0
#define DR_RESET       1
#define DR_HW_ADDR     2
#define DR_SENSOR      3
#define DR_INSERTED    4
#define DR_RAMMAP      5
#define DR_SIDE        6
#define DR_MAN_WRITE   7
#define DR_TRACK       8
#define DR_STATUS      9
#define DR_DISKCHANGE  12
#define DR_DRIVETYPE   13
#define DR_DIRTY       0x0800        // read: b7 any track written, b0 this track; write b7: clear "any", else clear this track
#define DR_PARAM       0x1000        // two 32 bit words per half track: DDR2 address; (length - 1) | bit time << 16. Side 1 at +0x400.
#define DR_WD177X      0x1800

#define DR_STAT_MOTOR     0x01
#define DR_STAT_WRITING   0x02
#define DR_STAT_WRITEBUSY 0x04

// the drive's fixed DDR2 areas (linker.x); the 64 KB image has RAM below and the ROM at +0x8000
#define DRIVE_A_AREA      0x00EE0000u
#define DRIVE_ROM_OFFSET  0x8000u
#define DRIVE_ROM_BYTES   0x8000u

enum DriveType { DT_1541 = 0, DT_1571 = 1, DT_1581 = 2, DT_UNSET = 3 };
enum MountMode { MM_RW = 0, MM_RO = 1, MM_UNLINKED = 2 };
enum DiskFormat { DF_D64 = 0, DF_G64 = 1, DF_D81 = 2 };
enum DiskState { DS_NONE = 0, DS_D64, DS_G64, DS_D81, DS_CLOSED };

class CbmDrive {
public:
    CbmDrive(CoreIo *io, CoreFs *fs);
    ~CbmDrive();

    // Probes the window and allocates the track memory. false and a sentence when the core has no drive (or no memory).
    bool attach(char *msg, int msglen);
    bool attached() const { return attached_; }
    bool multi_mode() const { return multi_; }               // the gateware can be a 1581 (CAPAB_MM_DRIVE: the drive type register takes a 2)

    // ---- the operations; each returns 0, or 1 and a sentence in msg ----
    int mount(const char *path, int mode, char *msg, int msglen);
    int eject(char *msg, int msglen);
    int save(const char *path, int format, char *msg, int msglen);
    // T180 D4: everything the head wrote is in the image's file, completely (the tracks an earlier write could not place are written again). 0, or 1 and the
    // reason in msg: the disk stays in the drive and the caller stops what it was about to do. An unlinked disk has no file and is no reason to stop.
    int flush(char *msg, int msglen);
    bool pending();                                           // there is something on its way to the file: for the "Saving media..." display
    // 0 created, 1 failed (msg says why). With `step` it returns 2 after each chunk written and wants the same call again (`made` then holds the
    // file being written); `cancel` on such a call drops the job, removes the partial file and returns 3. Without `step` it writes it all.
    int create(const char *path, int format, const char *name, char *msg, int msglen, char *made = 0, int madelen = 0, bool step = false,
               bool cancel = false, int *pct = 0);
    int put_rom(int type, const uint8_t *data, uint32_t n, char *msg, int msglen);    // a ROM of the type's 16 or 32 KB
    // A free name for a new image without its extension, for the menu's name prompt (T104.1 D15 scope): `stem`, `stem2` ... in the usual folder.
    bool suggest_name(const char *stem, const char *ext, char *out, int outlen);
    const char *image_stem(char *out, int outlen) const;       // the disk's name without its extension, "disk" when empty
    int set_rom_file(const char *path, char *msg, int msglen);
    int reset(char *msg, int msglen);                         // the drive's own reset: address, power cycle
    int set_power(bool on, char *msg, int msglen);
    void set_bus_id(int id);                                  // 8 .. 11
    void set_extra_ram(bool on);
    void set_swap_delay(int hundred_ms);                      // 1 .. 10
    void set_twist(int hundreds) { twist_ = hundreds * 100; }  // "Track Twist" 0 .. 40 (x 100), used by the next D64 mount
    void set_align(bool on) { align_ = on; }                   // "GCR Save Align Tracks": a G64 is written with every track starting at sector 0
    void set_reset_with_c64(bool on);                          // "Reset drive with C64": b1 of DR_RESET (c1541.cc:337-349 of TRX-Ultimate)
    int swap_delay_ms() const { return swap_ms_; }
    // T104 5.14: the core's partial bitstream was loaded again, so the drive's registers are at their configuration values. Writes what this
    // object knows (type, address, RAM map, the disk in the drive, power) into them. Nothing when the drive was never attached.
    void resync_hw();

    // One pass of the drive's service: tracks the head wrote go back to the image file, a 1581's commands are answered. true: it had work.
    bool poll();

    // ---- what is in it ----
    int state() const { return state_; }
    const char *image_name() const { return name_[0] ? name_ : 0; }          // null: nothing in the drive
    const char *mode_name() const;                                           // "rw", "ro", "unlinked", "empty"
    bool unsaved() const { return unsaved_; }
    int type() const { return type_; }
    bool powered() const { return powered_; }
    int bus_id() const { return bus_id_; }
    bool has_rom(int type) const { return type >= 0 && type < 3 && rom_have_[type]; }
    const char *last_dir() const { return last_dir_; }
    // T180 D2: the disk's full path (null: nothing in the drive) and how it was mounted (MountMode), for the stored mounts. on_change: called after a mount or an
    // eject that changed what is in the drive (the application stores the mount); `user` is handed back.
    const char *image_path() const { return name_[0] && path_[0] ? path_ : 0; }
    int mount_mode() const { return mode_; }
    void on_change(void (*fn)(void *user), void *user) { change_ = fn; change_user_ = user; }
    void set_new_dir(const char *dir);                      // the menu's folder: where Create and Save put a bare name, before the folder of the last disk

    // A sentence the application tells once (a write-back that failed), or null.
    const char *take_notice();

private:
    CoreIo *io_;
    CoreFs *fs_;
    bool attached_, multi_;
    int type_;
    bool powered_, want_power_;
    int bus_id_;
    bool extra_ram_;
    int swap_ms_;
    int twist_ = 2700;
    bool align_ = true;
    bool reset_with_c64_ = true;
    uint8_t rom_[3][DRIVE_ROM_BYTES];
    bool rom_have_[3];

    uint8_t *mem_;
    uint32_t phys_;
    uint32_t dummy_off_, wd_off_;
    GcrImage *gcr_;
    BinImage *bin_;
    WdController *wd_;

    CoreFile *file_;
    int state_;
    int mode_;
    bool protect_;
    char name_[FS_NAME_MAX + 1];     // the image's whole file name: Swap Disk finds the next one by it
    char last_dir_[200];
    char path_[FS_PATH_MAX];                // T180 D2: the path the disk was mounted from
    void (*change_)(void *user);
    void *change_user_;
    void changed() { if (change_) change_(change_user_); }
    char new_dir_[200];            // the folder the menu is on when a new image is asked for (T122 D2); empty: the folder of the last disk
    bool unsaved_;                  // the disk in the drive holds a change that no file has
    uint64_t failed_tr_;            // bit n: track n was written in the drive and the file did not take it (D64, G64); flush and eject write it again
    bool discard_armed_;            // the person was told once and asked again
    bool wrote_;                    // the head wrote something since the mount
    int write_skip_;
    char notice_[200];
    bool notice_pending_;
    bool wd_err_told_, wb_err_told_;
    NfWriter job_;                  // a stepped create (T122 §5): the file being written ...
    uint8_t *job_buf_;              // ... and the image it takes its chunks from
    char job_key_[300];
    uint32_t job_n_;
    void create_drop();
    bool build_new(const char *path, int format, const char *name, char *msg, int msglen);

    uint8_t r(uint32_t reg) { return io_->drv_rd(reg); }
    void w(uint32_t reg, uint8_t v) { io_->drv_wr(reg, v); }
    void tell(const char *s);
    void drive_reset_regs(uint8_t doit);
    void set_hw_address(int id);
    void set_sw_address(int id);
    void clear_params();
    void set_params();
    void remove_disk_regs();
    void insert_disk_regs(bool protect);
    int set_drive_type(int t, char *msg, int msglen);
    bool check_save_needed();
    int discard_gate(const char *what, char *msg, int msglen);
    void wait_for_writeback();
    bool settle();
    bool store_track(int tr);
    void close_file();
    void write_back_tracks();
    void forget();
    int mount_gcr(CoreFile *f, bool is_g64, bool protect, char *msg, int msglen);
    int mount_d81(CoreFile *f, bool protect, char *msg, int msglen);
    const char *new_pref() const;
};

#endif
