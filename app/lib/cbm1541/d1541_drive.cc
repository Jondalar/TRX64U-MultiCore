#include "d1541_drive.h"
#include "newfile.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#ifndef CLOCK_FREQ
#define CLOCK_FREQ 100000000          // the clock the drive's bit timer counts (the u64ii build's -DCLOCK_FREQ)
#endif
#define SENSOR_DARK  0
#define SENSOR_LIGHT 1
#define TRACK_TWIST  2700             // Gideon's default "Track Twist" 27 (x 100)
#define D81_BYTES    819200u
#define D81_BYTES_ERR 822400u

static const char *const type_names[3] = { "1541", "1571", "1581" };

static const char *ext_of(const char *path)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    return dot && (!slash || dot > slash) ? dot : "";
}

static const char *base_of(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

CbmDrive::CbmDrive(CoreIo *io, CoreFs *fs)
    : io_(io), fs_(fs), attached_(false), multi_(false), type_(DT_UNSET), powered_(false), want_power_(true), bus_id_(8), extra_ram_(false),
      swap_ms_(100), mem_(0), phys_(0), dummy_off_(0), wd_off_(0), gcr_(0), bin_(0), wd_(0), file_(0), state_(DS_NONE), mode_(MM_RW),
      protect_(false), unsaved_(false), failed_tr_(0), discard_armed_(false), wrote_(false), write_skip_(0), notice_pending_(false), wd_err_told_(false),
      wb_err_told_(false)
{
    job_buf_ = 0;
    job_n_ = 0;
    job_key_[0] = 0;
    memset(rom_, 0, sizeof(rom_));
    memset(rom_have_, 0, sizeof(rom_have_));
    name_[0] = 0;
    last_dir_[0] = 0;
    path_[0] = 0;
    change_ = 0;
    change_user_ = 0;
    new_dir_[0] = 0;
    notice_[0] = 0;
}

CbmDrive::~CbmDrive()
{
    close_file();
    create_drop();
    delete gcr_;
    delete bin_;
    delete wd_;
}

void CbmDrive::tell(const char *s)
{
    if (!notice_pending_) {
        snprintf(notice_, sizeof(notice_), "%s", s);
        notice_pending_ = true;
    }
}

const char *CbmDrive::take_notice()
{
    if (!notice_pending_) {
        return 0;
    }
    notice_pending_ = false;
    return notice_;
}

bool CbmDrive::attach(char *msg, int msglen)
{
    if (attached_) {
        return true;
    }
    if (!io_->drv_present()) {
        snprintf(msg, msglen, "This machine has no built-in drive.");
        return false;
    }
    // c1541.cc:119: a 2 in the drive type register that reads back as a 2 is a gateware that can be a 1581 (mm_drive); the 1541 only
    // gateware keeps no such register.
    w(DR_DRIVETYPE, 2);
    multi_ = (r(DR_DRIVETYPE) & 3) == 2;
    if (multi_) {
        w(DR_DRIVETYPE, 0);
    }
    uint32_t dummy_off = (GCR_MAXSIZE + 3u) & ~3u;
    uint32_t wd_off = (dummy_off + GCR_DUMMYTRACKLEN + 3u) & ~3u;
    uint32_t total = wd_off + (multi_ ? WD_BUFFER_BYTES : 0);
    mem_ = io_->drv_buffer(total, &phys_);
    if (!mem_) {
        snprintf(msg, msglen, "Out of memory for the drive's track images.");
        return false;
    }
    dummy_off_ = dummy_off;
    wd_off_ = wd_off;
    memset(mem_, 0, total);
    gcr_ = new GcrImage(mem_, phys_);
    if (multi_) {
        wd_ = new WdController(io_, mem_ + wd_off_, phys_ + wd_off_);
        wd_->init();
    }
    clear_params();
    w(DR_SENSOR, SENSOR_LIGHT);
    w(DR_INSERTED, 0);
    state_ = DS_NONE;
    attached_ = true;
    return true;
}

// ---- registers ----------------------------------------------------------------------------------------------------------------
void CbmDrive::drive_reset_regs(uint8_t doit)
{
    w(DR_RESET, doit);
    io_->delay_ms(1);
    w(DR_RESET, 6);                    // b1: the drive resets with the machine, b2: it stands still while the machine is held for the menu
}

void CbmDrive::set_hw_address(int id)
{
    w(DR_HW_ADDR, (uint8_t)(id & 3));
}

// The KERNAL of the 1541 reads its device number from the port at $EB3A into $77 / $78 (c1541.cc:359)
void CbmDrive::set_sw_address(int id)
{
    uint8_t v[2] = { (uint8_t)((id & 0x1F) + 0x20), (uint8_t)((id & 0x1F) + 0x40) };
    io_->ddr_abs_write(DRIVE_A_AREA + 0x77, &v[0], 1);
    io_->ddr_abs_write(DRIVE_A_AREA + 0x78, &v[1], 1);
}

void CbmDrive::set_bus_id(int id)
{
    if (id < 8) id = 8;
    if (id > 11) id = 11;
    bus_id_ = id;
    char m[8];
    if (attach(m, sizeof(m))) {
        set_hw_address(id);
        set_sw_address(id);
    }
}

void CbmDrive::set_extra_ram(bool on)
{
    extra_ram_ = on;
    char m[8];
    if (attach(m, sizeof(m))) {
        w(DR_RAMMAP, on ? 0x80 : 0x00);
    }
}

void CbmDrive::set_swap_delay(int hundred_ms)
{
    if (hundred_ms < 1) hundred_ms = 1;
    if (hundred_ms > 10) hundred_ms = 10;
    swap_ms_ = hundred_ms * 100;
}

// Every half track points at the empty track and says it is as long as a real one: an unformatted track, not a loop of 257 bytes
void CbmDrive::clear_params()
{
    uint32_t rotation = (uint32_t)CLOCK_FREQ / 20;
    uint32_t bit_time = rotation / GCR_DUMMYTRACKLEN;
    uint32_t dummy = phys_ + dummy_off_;
    memset(mem_ + dummy_off_, 0, GCR_DUMMYTRACKLEN);
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < 128; i++) {
            uint32_t a = DR_PARAM + (uint32_t)side * 0x400 + (uint32_t)i * 8;
            io_->drv_wr32(a, dummy);
            io_->drv_wr32(a + 4, (uint32_t)(GCR_DUMMYTRACKLEN - 1) | (bit_time << 16));
        }
    }
}

void CbmDrive::set_params()
{
    uint32_t rotation = (uint32_t)CLOCK_FREQ / 20;
    for (int i = 0; i < GCR_TRACKS; i++) {
        GcrTrack *tr = &gcr_->tracks[i];
        if (!tr->track_address || tr->track_length <= 0) {
            continue;                                         // stays on the empty track
        }
        uint32_t a = DR_PARAM + (uint32_t)i * 8;
        uint32_t bit_time = rotation / (uint32_t)tr->track_length;
        io_->drv_wr32(a, gcr_->phys_of(tr->track_address));
        io_->drv_wr32(a + 4, (uint32_t)(tr->track_length - 1) | (bit_time << 16));
    }
}

void CbmDrive::remove_disk_regs()
{
    w(DR_INSERTED, 0);
    w(DR_SENSOR, SENSOR_DARK);
    io_->delay_ms((uint32_t)swap_ms_);
    clear_params();
    w(DR_SENSOR, SENSOR_LIGHT);
    w(DR_DISKCHANGE, 1);
}

void CbmDrive::insert_disk_regs(bool protect)
{
    w(DR_SENSOR, SENSOR_DARK);
    io_->delay_ms(150);
    set_params();
    for (int tr = 0; tr < 42; tr++) {
        w(DR_DIRTY + (uint32_t)tr, 0);
    }
    w(DR_DIRTY, 0x80);
    if (!protect) {
        w(DR_SENSOR, SENSOR_LIGHT);
    }
    w(DR_INSERTED, 1);
    w(DR_DISKCHANGE, 1);
}

// ---- power, ROM, type -------------------------------------------------------------------------------------------------------------
int CbmDrive::set_drive_type(int t, char *msg, int msglen)
{
    if (!multi_ && t != DT_1541) {
        snprintf(msg, msglen, "The drive of this machine is a 1541 only.");
        return 1;
    }
    if (type_ == t) {
        return 0;
    }
    if (!rom_have_[t]) {
        snprintf(msg, msglen, "No ROM for the %s: put %s.rom into the core's roms folder.", type_names[t], type_names[t]);
        return 1;
    }
    if (multi_) {
        w(DR_DRIVETYPE, (uint8_t)t);
    }
    io_->ddr_abs_write(DRIVE_A_AREA + DRIVE_ROM_OFFSET, rom_[t], DRIVE_ROM_BYTES);
    type_ = t;
    if (powered_) {
        drive_reset_regs(1);
    }
    return 0;
}

int CbmDrive::set_power(bool on, char *msg, int msglen)
{
    if (!attach(msg, msglen)) {
        return 1;
    }
    want_power_ = on;
    if (!on) {
        w(DR_POWER, 0);
        powered_ = false;
        w(DR_DIRTY, 0x80);
        return 0;
    }
    if (type_ == DT_UNSET) {
        w(DR_POWER, 0);
        powered_ = false;
        snprintf(msg, msglen, "The drive stays off: no 1541 ROM (1541.rom in the core's roms folder).");
        return 1;
    }
    w(DR_POWER, 1);
    powered_ = true;
    drive_reset_regs(1);
    w(DR_DIRTY, 0x80);
    return 0;
}

void CbmDrive::resync_hw()
{
    if (!attached_) {
        return;
    }
    if (multi_) {
        w(DR_DRIVETYPE, type_ == DT_UNSET ? 0 : (uint8_t)type_);
    }
    set_hw_address(bus_id_);
    w(DR_RAMMAP, extra_ram_ ? 0x80 : 0x00);
    clear_params();
    w(DR_SENSOR, SENSOR_LIGHT);
    w(DR_INSERTED, 0);
    if (wd_) {
        wd_->init();
    }
    if (state_ == DS_D81) {
        w(DR_SENSOR, protect_ ? SENSOR_DARK : SENSOR_LIGHT);
        w(DR_INSERTED, 1);
        w(DR_DISKCHANGE, 1);
    } else if (state_ != DS_NONE) {
        insert_disk_regs(protect_);                           // the track addresses and lengths are in the object, the tracks in the DDR2
    }
    powered_ = false;
    if (want_power_ && type_ != DT_UNSET) {
        w(DR_POWER, 1);
        powered_ = true;
        drive_reset_regs(1);
    } else {
        w(DR_POWER, 0);
    }
    w(DR_DIRTY, 0x80);
}

int CbmDrive::reset(char *msg, int msglen)
{
    if (!attach(msg, msglen)) {
        return 1;
    }
    set_hw_address(bus_id_);
    return set_power(want_power_, msg, msglen);
}

int CbmDrive::put_rom(int type, const uint8_t *data, uint32_t n, char *msg, int msglen)
{
    if (!attach(msg, msglen)) {
        return 1;
    }
    if (type < 0 || type > DT_1581 || type == DT_1571) {
        snprintf(msg, msglen, "No such drive type.");
        return 1;
    }
    if (n != 16384 && n != 32768) {
        snprintf(msg, msglen, "A drive ROM is 16 or 32 KB, this one is %u bytes.", (unsigned)n);
        return 1;
    }
    if (type == DT_1581 && !multi_) {
        rom_have_[type] = false;                              // kept nowhere: a 1541 only drive never runs it
        return 0;
    }
    memcpy(rom_[type], data, n);
    if (n == 16384) {
        memcpy(rom_[type] + 16384, data, 16384);
    }
    rom_have_[type] = true;
    bool active = type_ == type || (type_ == DT_UNSET && type == DT_1541);
    if (!active) {
        return 0;
    }
    if (multi_) {
        w(DR_DRIVETYPE, (uint8_t)type);
    }
    io_->ddr_abs_write(DRIVE_A_AREA + DRIVE_ROM_OFFSET, rom_[type], DRIVE_ROM_BYTES);
    type_ = type;
    set_hw_address(bus_id_);
    set_sw_address(bus_id_);
    if (want_power_) {
        return set_power(true, msg, msglen);
    }
    return 0;
}

int CbmDrive::set_rom_file(const char *path, char *msg, int msglen)
{
    static uint8_t buf[DRIVE_ROM_BYTES];
    int n = fs_->load(path, buf, sizeof(buf));
    if (n < 0) {
        snprintf(msg, msglen, "Cannot read %s (or it is larger than 32 KB).", path);
        return 1;
    }
    return put_rom(type_ == DT_UNSET ? DT_1541 : type_, buf, (uint32_t)n, msg, msglen);
}

// ---- disks ----------------------------------------------------------------------------------------------------------------------
void CbmDrive::close_file()
{
    if (wd_) {
        wd_->unmount();
    }
    if (file_) {
        fs_->close(file_);
        file_ = 0;
    }
}

void CbmDrive::forget()
{
    state_ = DS_NONE;
    name_[0] = 0;
    path_[0] = 0;
    unsaved_ = false;
    wrote_ = false;
    write_skip_ = 0;
    failed_tr_ = 0;
    protect_ = false;
    mode_ = MM_RW;
}

bool CbmDrive::check_save_needed()
{
    return state_ != DS_NONE && (unsaved_ || (state_ == DS_CLOSED && wrote_));
}

int CbmDrive::discard_gate(const char *, char *msg, int msglen)
{
    if (!check_save_needed()) {
        discard_armed_ = false;
        return 0;
    }
    if (discard_armed_) {
        discard_armed_ = false;
        return 0;
    }
    discard_armed_ = true;
    snprintf(msg, msglen, "The disk in the drive holds changes that are in no file. Save it first, or do this again to throw them away.");
    return 1;
}

void CbmDrive::wait_for_writeback()
{
    // the hardware ends a write by itself after a timeout, so this ends even when the motor keeps spinning
    for (int i = 0; i < 80 && ((r(DR_DIRTY) & 0x80) || write_skip_); i++) {
        poll();
        io_->delay_ms(50);
    }
    poll();
}

int CbmDrive::eject(char *msg, int msglen)
{
    if (!attach(msg, msglen)) {
        return 1;
    }
    if (state_ == DS_NONE) {
        changed();                                            // T180 D2: an eject of the empty drive is the person's word: a stored mount of it goes
        return 0;
    }
    settle();                                                 // T180 D4: written back (again) before the gate asks
    if (discard_gate("eject", msg, msglen)) {
        return 1;
    }
    remove_disk_regs();
    close_file();
    forget();
    changed();                                                // T180 D2: the empty drive is stored
    return 0;
}

int CbmDrive::mount_gcr(CoreFile *f, bool is_g64, bool protect, char *msg, int msglen)
{
    if (is_g64) {
        if (!gcr_->load(f, msg, msglen)) {
            return 1;
        }
    } else {
        if (!bin_) {
            bin_ = new BinImage();
        }
        int rc = bin_->load(f);
        if (rc) {
            const char *why = rc == -4 ? "a .d71 needs the 1571; the drive is a single sided 1541"
                            : rc == -3 ? "not a .d64 (smaller than 35 tracks)"
                            : rc == -5 ? "not a .d64 (larger than 42 tracks)" : "cannot read the image (the medium is gone?)";
            snprintf(msg, msglen, "%s", why);
            return 1;
        }
        gcr_->from_bin(bin_, twist_);
    }
    insert_disk_regs(protect);
    return 0;
}

int CbmDrive::mount_d81(CoreFile *f, bool protect, char *msg, int msglen)
{
    uint64_t size = f->size();
    if (size < 10240 * 2 || (size % 10240 != 0 && size != D81_BYTES_ERR) || size / 10240 > WD_MAX_TRACKS_PER_SIDE) {
        snprintf(msg, msglen, "not a .d81 (%u bytes)", (unsigned)fs_size32(size));
        return 1;
    }
    wd_->mount(f, size == D81_BYTES_ERR ? D81_BYTES : size);
    w(DR_SENSOR, protect ? SENSOR_DARK : SENSOR_LIGHT);
    w(DR_INSERTED, 1);
    w(DR_DISKCHANGE, 1);
    return 0;
}

int CbmDrive::mount(const char *path, int mode, char *msg, int msglen)
{
    if (!attach(msg, msglen)) {
        return 1;
    }
    const char *ext = ext_of(path);
    int fmt = !strcasecmp(ext, ".d64") ? DF_D64 : !strcasecmp(ext, ".g64") ? DF_G64 : !strcasecmp(ext, ".d81") ? DF_D81 : -1;
    if (fmt < 0) {
        snprintf(msg, msglen, "%s is no disk image (.d64, .g64 or .d81).", base_of(path));
        return 1;
    }
    int need = fmt == DF_D81 ? DT_1581 : DT_1541;
    if (need == DT_1581 && !multi_) {
        snprintf(msg, msglen, "A .d81 needs a 1581: the drive gateware of this machine is a 1541 only.");
        return 1;
    }
    if (fmt == DF_D81 && mode == MM_UNLINKED) {
        snprintf(msg, msglen, "A .d81 cannot be mounted unlinked: the 1581 reads its sectors from the file. Mount it read only instead.");
        return 1;
    }
    if (state_ != DS_NONE) {
        settle();                                             // T180 D1, D4: what the head wrote is in the old disk's file before the gate asks
    }
    if (discard_gate("mount", msg, msglen)) {
        return 1;
    }
    CoreFile *f = fs_->open(path, mode == MM_RW);
    if (!f) {
        snprintf(msg, msglen, "Cannot open %s.", path);
        return 1;
    }
    bool protect = mode == MM_RO || (mode == MM_RW && !f->writable());
    if (need == DT_1581 || type_ != DT_UNSET) {
        if (set_drive_type(need, msg, msglen)) {
            fs_->close(f);
            return 1;
        }
    }
    w(DR_RESET, 0);                                           // the machine is held for the menu; the drive must see the disk change
    if (state_ != DS_NONE) {
        remove_disk_regs();
    }
    close_file();
    forget();
    int rc = fmt == DF_D81 ? mount_d81(f, protect, msg, msglen) : mount_gcr(f, fmt == DF_G64, protect, msg, msglen);
    if (rc) {
        fs_->close(f);
        drive_reset_regs(0);
        changed();                                            // T180 D2: the old disk is out and the drive is empty
        return 1;
    }
    snprintf(name_, sizeof(name_), "%s", base_of(path));
    snprintf(path_, sizeof(path_), "%s", path);
    size_t dl = (size_t)(base_of(path) - path);
    nf_copy(last_dir_, sizeof(last_dir_), path, dl > 1 ? dl - 1 : dl);
    mode_ = mode;
    protect_ = protect;
    discard_armed_ = false;
    if (mode == MM_UNLINKED) {
        fs_->close(f);
        state_ = DS_CLOSED;
    } else {
        file_ = f;
        state_ = fmt == DF_D64 ? DS_D64 : fmt == DF_G64 ? DS_G64 : DS_D81;
    }
    drive_reset_regs(0);                                      // no reset, but the freeze is back
    changed();                                                // T180 D2
    return 0;
}

// ---- the head wrote ------------------------------------------------------------------------------------------------------------
// One track of the image back into its file (both half tracks of it). false: the file did not take it; the track is remembered in failed_tr_
// (T180 D4) and the next flush writes it again from the drive's memory. A disk with no file behind it (unlinked) has nothing to write and nothing to remember.
bool CbmDrive::store_track(int tr)
{
    bool ok = true;
    bool any = false;
    for (int i = 2 * tr; i <= 2 * tr + 1; i++) {
        if (!gcr_->tracks[i].track_address) {
            continue;
        }
        any = true;
        if (state_ == DS_G64) {
            ok = ok && file_ && !protect_ && file_->writable() && gcr_->write_track(i, file_, align_);
        } else if (state_ == DS_D64 && !(i & 1)) {
            int errs = 0, secs = 0;
            int rc = bin_->write_track(i, gcr_, errs, secs);
            uint8_t *data = bin_->track_data(tr);
            ok = ok && rc == 0 && data && file_ && !protect_ && file_->writable() &&
                 file_->write(bin_->track_offset(tr), data, 256u * (uint32_t)bin_->track_sectors(tr)) && file_->sync();
        } else if (state_ != DS_D64) {
            ok = false;                                   // no file behind the disk: the change lives in the drive only
        }
    }
    if (any && !ok) {
        unsaved_ = true;
        if (state_ == DS_D64 || state_ == DS_G64) {
            failed_tr_ |= 1ull << tr;
        }
        if (state_ != DS_CLOSED && !wb_err_told_) {
            tell("Writing the changed disk back to its image file failed. The change stays in the drive; save the disk to keep it.");
            wb_err_told_ = true;
        }
        return false;
    }
    failed_tr_ &= ~(1ull << tr);
    return true;
}

void CbmDrive::write_back_tracks()
{
    int drive_track = r(DR_TRACK) >> 1;
    if (!write_skip_) {
        w(DR_DIRTY, 0x80);                                    // the flag is cleared once nothing was skipped any more
    }
    write_skip_ = 0;
    for (int tr = 0; tr < 42; tr++) {
        if (!(r(DR_DIRTY + (uint32_t)tr) & 1)) {
            continue;
        }
        for (int i = 2 * tr; i <= 2 * tr + 1; i++) {
            gcr_->track_got_written_with_gcr(i);
        }
        if ((r(DR_STATUS) & DR_STAT_WRITEBUSY) && drive_track == tr) {
            write_skip_++;                                    // still writing this track: later
            continue;
        }
        w(DR_DIRTY + (uint32_t)tr, 0);
        wrote_ = true;
        store_track(tr);
    }
}

// T180 D4: what the head wrote is in the file, completely. The hardware finishes its write (wait_for_writeback), the tracks an earlier write could not
// place are written again; false: one is still not in the file (failed_tr_) or the drive still writes.
bool CbmDrive::settle()
{
    wait_for_writeback();
    if (state_ == DS_D64 || state_ == DS_G64) {
        for (int tr = 0; tr < 42; tr++) {
            if (failed_tr_ & (1ull << tr)) {
                store_track(tr);
            }
        }
        if (!failed_tr_) {
            unsaved_ = false;                                 // the disk is in its file again; the next failure tells again
            wb_err_told_ = false;
        }
    }
    return !((r(DR_DIRTY) & 0x80) || write_skip_) && !(state_ != DS_NONE && state_ != DS_CLOSED && unsaved_);
}

int CbmDrive::flush(char *msg, int msglen)
{
    if (!attached_ || state_ == DS_NONE) {
        return 0;
    }
    if (settle()) {                                          // an unlinked disk has no file: its changes are the person's choice (the gate of eject), settle() does not count them
        return 0;
    }
    if (state_ == DS_D81) {
        snprintf(msg, msglen, "The 1581 disk could not be written back to its .d81 file. It stays in the drive; eject it twice to throw the change away.");
    } else if (unsaved_) {
        snprintf(msg, msglen, "The disk in the drive could not be written back to its image file. It stays in the drive; save it to a file or put the medium back.");
    } else {
        snprintf(msg, msglen, "The drive is still writing the disk. It stays in the drive.");
    }
    return 1;
}

bool CbmDrive::pending()
{
    if (!attached_ || state_ == DS_NONE || state_ == DS_CLOSED) {
        return false;
    }
    return unsaved_ || failed_tr_ || write_skip_ || (r(DR_DIRTY) & 0x80);
}

bool CbmDrive::poll()
{
    if (!attached_) {
        return false;
    }
    bool did = false;
    if (wd_ && state_ == DS_D81) {
        did = wd_->poll();
        if (wd_->take_write_errors()) {
            unsaved_ = true;
            if (!wd_err_told_) {
                tell("Writing a sector of the 1581 disk back to its .d81 file failed.");
                wd_err_told_ = true;
            }
        }
    }
    if ((r(DR_DIRTY) & 0x80) || write_skip_) {
        if (state_ == DS_NONE) {
            w(DR_DIRTY, 0x80);
        } else {
            write_back_tracks();
            did = true;
        }
    }
    return did;
}

// ---- save and create --------------------------------------------------------------------------------------------------------------
// The folders and names of a new file are the shared rule of newfile.h (T122 D2); the folder the menu is on comes before the folder of the
// last disk.
const char *CbmDrive::new_pref() const
{
    return new_dir_[0] ? new_dir_ : last_dir_;
}

void CbmDrive::set_new_dir(const char *dir)
{
    snprintf(new_dir_, sizeof(new_dir_), "%s", dir ? dir : "");
}

// A free name for a new image, without its extension: `stem`, `stem2` ... in the first folder the browser can read. The menu starts its name
// prompt with it.
bool CbmDrive::suggest_name(const char *stem, const char *ext, char *out, int outlen)
{
    return nf_suggest(fs_, new_pref(), stem, ext, out, outlen);
}

int CbmDrive::save(const char *path, int format, char *msg, int msglen)
{
    if (!attach(msg, msglen)) {
        return 1;
    }
    if (state_ == DS_NONE) {
        snprintf(msg, msglen, "There is no disk in the drive.");
        return 1;
    }
    if (state_ == DS_D81 || format == DF_D81) {
        snprintf(msg, msglen, "A .d81 is written to its file as the 1581 writes it; saving is for the GCR disks of a 1541.");
        return 1;
    }
    wait_for_writeback();
    char stem[80], base[60];
    snprintf(stem, sizeof(stem), "%s_saved", image_stem(base, sizeof(base)));
    const char *ext = format == DF_G64 ? ".g64" : ".d64";
    uint8_t *buf = 0;
    uint32_t n = 0;
    int errors = 0;
    if (format == DF_G64) {
        uint32_t cap = GCR_MAXSIZE + 8192;
        buf = new uint8_t[cap];
        n = gcr_->build_g64(buf, cap, align_);
    } else {
        BinImage *tmp = new BinImage();
        errors = gcr_->to_bin(tmp);
        n = tmp->file_bytes();
        buf = new uint8_t[n];
        tmp->build(buf);
        delete tmp;
    }
    int rc = 0;
    char full[FS_PATH_MAX];
    full[0] = 0;
    if (!n) {
        snprintf(msg, msglen, "The disk does not fit into a .g64.");
        rc = 1;
    } else if (nf_write_new(fs_, new_pref(), path, stem, ext, buf, n, full, sizeof(full), msg, msglen)) {
        rc = 1;
    } else {
        unsaved_ = false;                                     // the change is in the new file; it is not owed to the old one any more (failed_tr_)
        failed_tr_ = 0;
        discard_armed_ = false;
        if (errors) {
            snprintf(msg, msglen, "Saved %s; %d sector(s) did not decode clean and carry error bytes.", base_of(full), errors);
        } else {
            snprintf(msg, msglen, "Saved %s (%u bytes).", base_of(full), (unsigned)n);
        }
    }
    delete[] buf;
    return rc;
}

void CbmDrive::create_drop()
{
    job_.abort();
    delete[] job_buf_;
    job_buf_ = 0;
    job_key_[0] = 0;
}

int CbmDrive::create(const char *path, int format, const char *name, char *msg, int msglen, char *made, int madelen, bool step, bool cancel,
                     int *pct)
{
    char key[sizeof(job_key_)];
    snprintf(key, sizeof(key), "%d|%s|%s", format, path ? path : "", name ? name : "");
    bool going = job_buf_ && !strcmp(key, job_key_);
    if (going && cancel) {
        create_drop();
        snprintf(msg, msglen, "Cancelled: nothing was written.");
        return 3;
    }
    int rc;
    if (going && step) {
        rc = job_.step(msg, msglen);
    } else {
        create_drop();
        if (!build_new(path, format, name, msg, msglen)) {
            return 1;
        }
        const char *ext = format == DF_G64 ? ".g64" : format == DF_D81 ? ".d81" : ".d64";
        rc = job_.begin(fs_, new_pref(), path, "blank", ext, job_buf_, job_n_, msg, msglen);
        snprintf(job_key_, sizeof(job_key_), "%s", key);
    }
    while (!step && rc == NF_MORE) {
        rc = job_.step(msg, msglen);
    }
    if (pct) {
        *pct = job_.percent();
    }
    if (rc == NF_MORE) {
        if (made) {
            snprintf(made, madelen, "%s", job_.full());
        }
        return 2;
    }
    char full[FS_PATH_MAX];
    snprintf(full, sizeof(full), "%s", job_.full());
    create_drop();
    if (rc == NF_FAIL) {
        return 1;
    }
    snprintf(msg, msglen, "Created %s.", full);
    if (made) {
        snprintf(made, madelen, "%s", full);
    }
    if (pct) {
        *pct = 100;
    }
    return 0;
}

// The image of a new disk into job_buf_ (job_n_ bytes); false and a sentence when it cannot be built.
bool CbmDrive::build_new(const char *path, int format, const char *name, char *msg, int msglen)
{
    uint32_t cap = format == DF_D81 ? D81_BYTES : GCR_MAXSIZE + 8192;
    if (format == DF_D64) {
        cap = D64_BUF_SIZE;
    }
    uint8_t *buf = new uint8_t[cap];
    char vol[40];
    // the volume name: the one given, or the file's name; at most 16 characters (",ID" behind the name is the disk ID)
    if (name && name[0]) {
        snprintf(vol, sizeof(vol), "%s", name);
    } else {
        const char *b = base_of(path && path[0] ? path : "blank");
        nf_copy(vol, sizeof(vol), b, strcspn(b, "."));
        if (!vol[0]) snprintf(vol, sizeof(vol), "blank");
    }
    uint32_t n = 0;
    if (format == DF_D64) {
        n = d64_blank(buf, cap, vol);
    } else if (format == DF_D81) {
        n = d81_blank(buf, cap, vol);
    } else {
        BinImage *b = new BinImage();
        b->format(vol, 35);
        uint8_t *gm = new uint8_t[GCR_MAXSIZE];
        GcrImage *g = new GcrImage(gm, 0);
        g->from_bin(b, 0);
        n = g->build_g64(buf, cap, false);
        delete g;
        delete[] gm;
        delete b;
    }
    if (!n) {
        snprintf(msg, msglen, "Cannot build the image.");
        delete[] buf;
        return false;
    }
    job_buf_ = buf;
    job_n_ = n;
    return true;
}

const char *CbmDrive::image_stem(char *out, int outlen) const
{
    if (!name_[0]) {
        snprintf(out, (size_t)outlen, "disk");
    } else {
        nf_copy(out, outlen, name_, strcspn(name_, "."));
    }
    return out;
}

const char *CbmDrive::mode_name() const
{
    if (state_ == DS_NONE) {
        return "empty";
    }
    if (mode_ == MM_UNLINKED) {
        return "unlinked";
    }
    return mode_ == MM_RO || protect_ ? "ro" : "rw";
}
