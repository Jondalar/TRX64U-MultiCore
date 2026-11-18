// The Ultimate Command Interface's targets: PROVENANCE host-app/software/io/command_interface (the server and the NETWORK target, the firmware's own) and the firmware's
// filemanager/dos.cc (DOS 1 and 2) and io/command_interface/control_target.cc (CONTROL 4), GPL-3.0, Gideon Zweijtzer's firmware as TRX64-Ultimate carries it. What changed:
// the drives, the REU and the time come through UciHost / the operations (the core app has no C1541 object), the write refusal of the core app (fs_write_refused: a core
// application never writes /flash) applies to every write a C64 asks for, the REU memory is reached through CoreIo::ddr_* (the data cache), and the targets are made by
// uci_start() instead of by static constructors. The commands the machine cannot answer are listed at the end of the file.
#include "uci.h"
#include "ops_glue.h"
#include "home.h"
#include "fsif.h"
#include "command_intf.h"
#include "network_target.h"
#include "filemanager.h"
#include "itu.h"
#include "rtc.h"
#include "product.h"
#include "endianness.h"
#include "palette_command.h"
#include "settings_ops.h"
#include "iomap.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

#define UCI_REU_BYTES 0x01000000u

static CoreCtx *g_ctx;
static const UciHost *g_host;

static const char *const wdnames[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// ---- the REU's memory, in chunks through the data-cache-safe door ------------------------------------------------------------------------------------------
static uint32_t reu_from_file(File *f, uint32_t addr, uint32_t len, FRESULT *res)
{
    uint8_t *buf = new uint8_t[4096];
    uint32_t done = 0;
    *res = FR_OK;
    while (done < len) {
        uint32_t n = len - done < 4096 ? len - done : 4096, got = 0;
        *res = f->read(buf, n, &got);
        if (got) {
            g_ctx->io->ddr_write(addr + done, buf, got);
        }
        done += got;
        if (*res != FR_OK || got != n) {
            break;
        }
    }
    delete[] buf;
    return done;
}

static uint32_t reu_to_file(File *f, uint32_t addr, uint32_t len, FRESULT *res)
{
    uint8_t *buf = new uint8_t[4096];
    uint32_t done = 0;
    *res = FR_OK;
    while (done < len) {
        uint32_t n = len - done < 4096 ? len - done : 4096, put = 0;
        g_ctx->io->ddr_read(addr + done, buf, n);
        *res = f->write(buf, n, &put);
        done += put;
        if (*res != FR_OK || put != n) {
            break;
        }
    }
    delete[] buf;
    return done;
}

// ---- DOS (dos.cc) --------------------------------------------------------------------------------------------------------------------------------------
#define DOS_CMD_IDENTIFY       0x01
#define DOS_CMD_OPEN_FILE      0x02
#define DOS_CMD_CLOSE_FILE     0x03
#define DOS_CMD_READ_DATA      0x04
#define DOS_CMD_WRITE_DATA     0x05
#define DOS_CMD_FILE_SEEK      0x06
#define DOS_CMD_FILE_INFO      0x07
#define DOS_CMD_FILE_STAT      0x08
#define DOS_CMD_DELETE_FILE    0x09
#define DOS_CMD_RENAME_FILE    0x0a
#define DOS_CMD_COPY_FILE      0x0b
#define DOS_CMD_CHANGE_DIR     0x11
#define DOS_CMD_GET_PATH       0x12
#define DOS_CMD_OPEN_DIR       0x13
#define DOS_CMD_READ_DIR       0x14
#define DOS_CMD_COPY_UI_PATH   0x15
#define DOS_CMD_CREATE_DIR     0x16
#define DOS_CMD_COPY_HOME_PATH 0x17
#define DOS_CMD_LOAD_REU       0x21
#define DOS_CMD_SAVE_REU       0x22
#define DOS_CMD_MOUNT_DISK     0x23
#define DOS_CMD_UMOUNT_DISK    0x24
#define DOS_CMD_SWAP_DISK      0x25
#define DOS_CMD_GET_TIME       0x26
#define DOS_CMD_SET_TIME       0x27
#define DOS_CMD_ECHO           0xF0
#define DOS_CMD_LOAD_INTO_RAMDISK 0x41                                      // dos.h:36-37 (named CTRL_CMD_ there)
#define DOS_CMD_SAVE_RAMDISK      0x42

static Message c_message_identification_dos = { 20, true, (uint8_t *)"ULTIMATE-II DOS V1.2" };
static Message c_status_directory_empty     = { 18, true, (uint8_t *)"01,DIRECTORY EMPTY" };
static Message c_status_truncated           = { 20, true, (uint8_t *)"02,REQUEST TRUNCATED" };
static Message c_status_not_implemented     = { 27, true, (uint8_t *)"99,FUNCTION NOT IMPLEMENTED" };
static Message c_status_no_data             = { 19, true, (uint8_t *)"81,NOT IN DATA MODE" };
static Message c_status_file_not_found      = { 17, true, (uint8_t *)"82,FILE NOT FOUND" };
static Message c_status_no_such_dir         = { 20, true, (uint8_t *)"83,NO SUCH DIRECTORY" };
static Message c_status_no_file_to_close    = { 19, true, (uint8_t *)"84,NO FILE TO CLOSE" };
static Message c_status_file_not_open       = { 15, true, (uint8_t *)"85,NO FILE OPEN" };
static Message c_status_cannot_read_dir     = { 23, true, (uint8_t *)"86,CAN'T READ DIRECTORY" };
static Message c_status_internal_error      = { 17, true, (uint8_t *)"87,INTERNAL ERROR" };
static Message c_status_not_a_disk_image    = { 19, true, (uint8_t *)"89,NOT A DISK IMAGE" };
static Message c_status_drive_not_present   = { 20, true, (uint8_t *)"90,DRIVE NOT PRESENT" };
static Message c_status_incompatible_image  = { 21, true, (uint8_t *)"91,INCOMPATIBLE IMAGE" };

typedef struct _dos_info {
    uint32_t size;
    uint16_t date;
    uint16_t time;
    char extension[3];
    uint8_t attrib;
    char filename[64];
} t_dos_info;

enum e_dos_state { e_dos_idle, e_dos_in_file, e_dos_in_directory };

class Dos : CommandTarget
{
    FileManager *fm;
    e_dos_state dos_state;
    t_dos_info dos_info;
    File *file;
    Path *path;
    IndexedList<FileInfo *> directoryList;
    Message data_message;
    Message status_message;
    int remaining;
    int dir_entries;
    int current_index;

    void cleanupDirectory()
    {
        for (int i = 0; i < directoryList.get_elements(); i++) {
            delete directoryList[i];
        }
        directoryList.clear_list();
    }
    // The core application never writes the flash (T93 D11): a write, delete, rename, new folder or copy target under /flash is refused as a write protected medium.
    bool refused(const char *name)
    {
        Path *t = new Path(path);
        t->cd(name);
        bool r = fs_write_refused(t->get_path());
        delete t;
        return r;
    }
    void fres_status(FRESULT res, Message **status)
    {
        strcpy((char *)status_message.message, FileSystem::get_error_string(res));
        status_message.length = strlen((char *)status_message.message);
        *status = &status_message;
    }
    void cd(Message *command, Message **reply, Message **status)
    {
        mstring old_path(path->get_path());
        *reply = &c_message_empty;
        command->message[command->length] = 0;
        path->cd((char *)&command->message[2]);
        cleanupDirectory();
        FRESULT fres = fm->get_directory(path, directoryList, NULL);
        if (fres == FR_OK) {
            *status = &c_status_ok;
        } else {
            *status = &c_status_no_such_dir;
            path->cd(old_path.c_str());
        }
    }
    // dos.cc getDriveByID: id 0 = the first drive, else the one whose IEC address is id and which is powered.
    int driveByID(uint8_t id)
    {
        for (int i = 0; i < 2; i++) {
            uint8_t type, addr;
            bool power;
            if (!g_host->drive_info || !g_host->drive_info(g_ctx, i, &type, &addr, &power)) {
                continue;
            }
            if (id ? (power && addr == id) : true) {
                return i;
            }
        }
        return -1;
    }
    void fill_info(FileInfo *ffi, Message **reply)
    {
        dos_info.size = cpu_to_32le(ffi->size32());
        dos_info.date = cpu_to_16le(ffi->date);
        dos_info.time = cpu_to_16le(ffi->time);
        dos_info.extension[0] = ' ';
        dos_info.extension[1] = ' ';
        dos_info.extension[2] = ' ';
        strncpy(dos_info.extension, ffi->extension, 3);
        dos_info.attrib = ffi->attrib;
        strncpy(dos_info.filename, ffi->lfname, 63);
        data_message.length = strlen(dos_info.filename) + 4 + 2 + 2 + 4;
        data_message.last_part = true;
        memcpy(data_message.message, &dos_info, data_message.length);
        *reply = &data_message;
    }
    // A mount, eject or swap through the machine's own drive (UciHost): 0 done, else the status.
    void drive_do(int idx, int what, const char *file_path, Message **status)
    {
        char msg[120];
        msg[0] = 0;
        if (g_host->drive_do(g_ctx, idx, what, file_path, msg, sizeof(msg)) != 0) {
            *status = &c_status_incompatible_image;
        }
    }

    // dos.cc:578-740: a file into / out of a MegaPatch 3 RAM drive of the REU. The drive's type, size and first bank come from the machine (UciHost::ramdrive);
    // the file's bytes go through the data-cache-safe door (a 1571's second side lies 700 sectors behind the first).
    static int ext_type(const char *name)
    {
        const char *dot = strrchr(name, '.');
        if (!dot) {
            return 0;
        }
        return !strcasecmp(dot, ".d64") ? 1541 : !strcasecmp(dot, ".d71") ? 1571 : !strcasecmp(dot, ".d81") ? 1581 : !strcasecmp(dot, ".dnp") ? DRVTYPE_MP3_DNP : 0;
    }
    void ramdisk(Message *command, bool load, Message **reply, Message **status)
    {
        *reply = &c_message_empty;
        *status = &c_status_ok;
        command->message[command->length] = 0;
        int drvNo = command->message[2];
        bool whatIf = false;
        if (load && (drvNo & 0x80)) {
            drvNo &= 0x7F;
            whatIf = true;
        }
        char *filename = (char *)&command->message[3];
        FileInfo *ffi = 0;
        if (load) {
            ffi = new FileInfo(INFO_SIZE);
            if (fm->fstat(path, filename, *ffi) != FR_OK) {
                *status = &c_status_file_not_found;
                delete ffi;
                return;
            }
        }
        int ftype = 0;
        uint32_t expSize = 1, base = 0;
        if (g_host->ramdrive && drvNo < 4) {
            g_host->ramdrive(g_ctx, drvNo, &ftype, &expSize, &base);       // a native drive's size comes from the machine; the others have a fixed one
        }
        // dos.cc:605-618 compares the type of the file's extension with the drive's. A drive that is not there (type 0) and a file of no known type are equal in the firmware, which
        // then moves one byte to the first bank of nothing; here the drive must exist.
        if (ftype == 0 || ext_type(filename) != ftype) {
            *status = &c_status_incompatible_image;
            delete ffi;
            return;
        }
        if (ftype == 1541) {
            expSize = 174848;
        } else if (ftype == 1571) {
            expSize = 2 * 174848;
        } else if (ftype == 1581) {
            expSize = 819200;
        }
        if (load && ftype == DRVTYPE_MP3_DNP && expSize != (uint32_t)ffi->size32()) {
            *status = &c_status_incompatible_image;
            delete ffi;
            return;
        }
        if (expSize > 12u * 1024u * 1024u || (base >> 16) > 0x40) {
            *status = &c_status_incompatible_image;
            delete ffi;
            return;
        }
        delete ffi;
        if (whatIf) {
            return;
        }
        uint32_t at = base;                                                 // the first byte of the drive: bank << 16
        if (!load && refused(filename)) {
            *status = &c_status_internal_error;                             // dos.cc answers 87 whenever the file cannot be opened
            return;
        }
        File *f = 0;
        fm->fopen(path, filename, load ? FA_READ : (FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS), &f);
        if (!f) {
            *status = &c_status_internal_error;
            return;
        }
        uint8_t *buf = new uint8_t[4096];
        int parts = ftype == 1571 ? 2 : 1;
        uint32_t half = ftype == 1571 ? expSize / 2 : expSize;
        for (int part = 0; part < parts; part++) {
            uint32_t dst = at + (part ? 700 * 256 : 0);
            for (uint32_t done = 0; done < half;) {
                uint32_t n = half - done < 4096 ? half - done : 4096, got = 0;
                if (load) {
                    f->read(buf, n, &got);                                  // dos.cc ignores the result: a short file leaves the rest of the drive as it was
                    if (got) {
                        g_ctx->io->ddr_write(dst + done, buf, got);
                    }
                    if (got != n) {
                        done = half;
                    } else {
                        done += n;
                    }
                } else {
                    g_ctx->io->ddr_read(dst + done, buf, n);
                    f->write(buf, n, &got);
                    done += n;
                }
            }
        }
        delete[] buf;
        fm->fclose(f);
    }

public:
    Dos(int id) : directoryList(16, NULL)
    {
        command_targets[id] = this;
        data_message.message = new uint8_t[512];
        status_message.message = new uint8_t[80];
        fm = FileManager::getFileManager();
        path = fm->get_new_path("Dos");
        file = 0;
        dir_entries = remaining = current_index = 0;
        dos_state = e_dos_idle;
    }
    ~Dos()
    {
        fm->release_path(path);
        delete[] data_message.message;
        delete[] status_message.message;
    }

    void parse_command(Message *command, Message **reply, Message **status)
    {
        uint32_t pos, addr, len;
        uint32_t transferred = 0;
        FRESULT res = FR_OK;
        FileInfo *ffi;
        char *oldname, *newname, *filename, *destination;

        switch (command->message[1]) {
        case DOS_CMD_IDENTIFY:
            *reply = &c_message_identification_dos;
            *status = &c_status_ok;
            break;
        case DOS_CMD_OPEN_FILE:
            command->message[command->length] = 0;
            *reply = &c_message_empty;
            file = NULL;
            if ((command->message[2] & (FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS | 0x10)) && refused((char *)&command->message[3])) {
                res = FR_WRITE_PROTECTED;
            } else {
                res = fm->fopen(path, (char *)&command->message[3], command->message[2], &file);
            }
            if (!file) {
                fres_status(res, status);
            } else {
                *status = &c_status_ok;
            }
            break;
        case DOS_CMD_CLOSE_FILE:
            *reply = &c_message_empty;
            if (file) {
                fm->fclose(file);
                file = NULL;
                *status = &c_status_ok;
            } else {
                *status = &c_status_no_file_to_close;
            }
            break;
        case DOS_CMD_FILE_SEEK:
            *reply = &c_message_empty;
            *status = &c_status_ok;
            if (!file) {
                *status = &c_status_file_not_open;
            } else {
                pos = (((uint32_t)command->message[5]) << 24) | (((uint32_t)command->message[4]) << 16) | (((uint32_t)command->message[3]) << 8) | command->message[2];
                res = file->seek(pos);
                if (res != FR_OK) {
                    fres_status(res, status);
                }
            }
            break;
        case DOS_CMD_FILE_INFO:
            *reply = &c_message_empty;
            if (!file) {
                *status = &c_status_file_not_open;
                break;
            }
            *status = &c_status_ok;
            ffi = new FileInfo(INFO_SIZE);
            res = fm->fstat(file->get_path(), *ffi);
            if (res != FR_OK) {
                *status = &c_status_file_not_found;
            } else {
                fill_info(ffi, reply);
            }
            delete ffi;
            break;
        case DOS_CMD_FILE_STAT:
            *reply = &c_message_empty;
            *status = &c_status_ok;
            command->message[command->length] = 0;
            ffi = new FileInfo(INFO_SIZE);
            res = fm->fstat(path, (char *)&command->message[2], *ffi);
            if (res != FR_OK) {
                *status = &c_status_file_not_found;
            } else {
                fill_info(ffi, reply);
            }
            delete ffi;
            break;
        case DOS_CMD_DELETE_FILE:
            *reply = &c_message_empty;
            command->message[command->length] = 0;
            res = refused((char *)&command->message[2]) ? FR_WRITE_PROTECTED : fm->delete_file(path, (char *)&command->message[2]);
            if (res == FR_OK) {
                *status = &c_status_ok;
            } else {
                fres_status(res, status);
            }
            break;
        case DOS_CMD_RENAME_FILE:
            *reply = &c_message_empty;
            command->message[command->length] = 0;
            oldname = (char *)&command->message[2];
            newname = (char *)&command->message[2 + strlen(oldname) + 1];
            res = (refused(oldname) || refused(newname)) ? FR_WRITE_PROTECTED : fm->rename(path, oldname, newname);
            if (res == FR_OK) {
                *status = &c_status_ok;
            } else {
                fres_status(res, status);
            }
            break;
        case DOS_CMD_COPY_FILE:
            *reply = &c_message_empty;
            command->message[command->length] = 0;
            filename = (char *)&command->message[2];
            destination = (char *)&command->message[2 + strlen(filename) + 1];
            res = refused(destination) ? FR_WRITE_PROTECTED : fm->fcopy(path->get_path(), filename, destination, filename, false);
            if (res == FR_OK) {
                *status = &c_status_ok;
            } else {
                fres_status(res, status);
            }
            break;
        case DOS_CMD_LOAD_REU:
        case DOS_CMD_SAVE_REU: {
            bool load = command->message[1] == DOS_CMD_LOAD_REU;
            if (!file) {
                *status = &c_status_file_not_open;
                *reply = &c_message_empty;
                break;
            }
            *status = &c_status_ok;
            addr = (((uint32_t)command->message[5]) << 24) | (((uint32_t)command->message[4]) << 16) | (((uint32_t)command->message[3]) << 8) | command->message[2];
            len = (((uint32_t)command->message[9]) << 24) | (((uint32_t)command->message[8]) << 16) | (((uint32_t)command->message[7]) << 8) | command->message[6];
            addr &= 0x00FFFFFF;
            len &= 0x01FFFFFF;                                              // one bit more, to save the whole 16 MB REU
            if ((addr + len) > UCI_REU_BYTES) {
                len = UCI_REU_BYTES - addr;
                *status = &c_status_truncated;
            }
            transferred = load ? reu_from_file(file, addr, len, &res) : reu_to_file(file, addr, len, &res);
            *reply = &data_message;
            if (load) {
                sprintf((char *)data_message.message, "$%6x BYTES LOADED TO REU $%6x", transferred, addr);
                data_message.length = 35;
            } else {
                sprintf((char *)data_message.message, "$%6x BYTES SAVED FROM REU $%6x", transferred, addr);
                data_message.length = 36;
            }
            data_message.last_part = true;
            if (res != FR_OK) {
                fres_status(res, status);
            }
            break;
        }
        case DOS_CMD_LOAD_INTO_RAMDISK:
        case DOS_CMD_SAVE_RAMDISK:
            ramdisk(command, command->message[1] == DOS_CMD_LOAD_INTO_RAMDISK, reply, status);
            break;
        case DOS_CMD_MOUNT_DISK: {
            *reply = &c_message_empty;
            *status = &c_status_ok;
            command->message[command->length] = 0;
            uint8_t drive_id = command->message[2];
            filename = (char *)&command->message[3];
            int drive = driveByID(drive_id);
            if (drive < 0) {
                *status = &c_status_drive_not_present;
                break;
            }
            ffi = new FileInfo(INFO_SIZE);
            res = fm->fstat(path, filename, *ffi);
            if (res != FR_OK) {
                *status = &c_status_file_not_found;
                delete ffi;
                break;
            }
            int image_type;
            if (strncasecmp(ffi->extension, "d64", 3) == 0 || strncasecmp(ffi->extension, "g64", 3) == 0) {
                image_type = 1541;
            } else if (strncasecmp(ffi->extension, "d71", 3) == 0 || strncasecmp(ffi->extension, "g71", 3) == 0) {
                *status = &c_status_incompatible_image;              // no 1571 here: .d71 / .g71 are out (T104.1, Alex: "ruled out from the start")
                delete ffi;
                break;
            } else if (strncasecmp(ffi->extension, "d81", 3) == 0) {
                image_type = 1581;
            } else {
                *status = &c_status_not_a_disk_image;
                delete ffi;
                break;
            }
            delete ffi;
            if (!(getFpgaCapabilities() & CAPAB_MM_DRIVE) && (image_type != 1541)) {
                *status = &c_status_incompatible_image;
                break;
            }
            Path *t = new Path(path);
            t->cd(filename);
            drive_do(drive, 0, t->get_path(), status);
            delete t;
            break;
        }
        case DOS_CMD_UMOUNT_DISK:
        case DOS_CMD_SWAP_DISK: {
            *reply = &c_message_empty;
            *status = &c_status_ok;
            int drive = driveByID(command->message[2]);
            if (drive < 0) {
                *status = &c_status_drive_not_present;
                break;
            }
            drive_do(drive, command->message[1] == DOS_CMD_UMOUNT_DISK ? 1 : 2, NULL, status);
            break;
        }
        case DOS_CMD_CHANGE_DIR:
            cd(command, reply, status);
            break;
        case DOS_CMD_COPY_UI_PATH:
            *reply = &c_message_empty;
            *status = &c_status_not_implemented;
            break;
        case DOS_CMD_COPY_HOME_PATH:
            destination = (char *)home_dir(g_ctx);
            strncpy((char *)command->message + 2, destination, strlen(destination) + 1);
            command->length = 2 + strlen(destination) + 1;
            cd(command, reply, status);
            // fallthrough
        case DOS_CMD_GET_PATH:
            strcpy((char *)data_message.message, path->get_path());
            data_message.length = strlen((char *)data_message.message);
            data_message.last_part = true;
            *reply = &data_message;
            *status = &c_status_ok;
            break;
        case DOS_CMD_CREATE_DIR:
            *reply = &c_message_empty;
            command->message[command->length] = 0;
            res = refused((char *)&command->message[2]) ? FR_WRITE_PROTECTED : fm->create_dir(path, (char *)&command->message[2]);
            if (res == FR_OK) {
                *status = &c_status_ok;
            } else {
                fres_status(res, status);
            }
            break;
        case DOS_CMD_OPEN_DIR:
            cleanupDirectory();
            res = fm->get_directory(path, directoryList, NULL);
            *reply = &c_message_empty;
            if (res != FR_OK) {
                *status = &c_status_cannot_read_dir;
                break;
            }
            dir_entries = directoryList.get_elements();
            *status = dir_entries == 0 ? &c_status_directory_empty : &c_status_ok;
            break;
        case DOS_CMD_READ_DIR:
            current_index = 0;
            dos_state = e_dos_in_directory;
            get_more_data(reply, status);
            break;
        case DOS_CMD_READ_DATA:
            if (!file) {
                *reply = &c_message_empty;
                *status = &c_status_file_not_open;
            } else {
                remaining = (((int)command->message[3]) << 8) | command->message[2];
                dos_state = e_dos_in_file;
                get_more_data(reply, status);
            }
            break;
        case DOS_CMD_WRITE_DATA:
            *reply = &c_message_empty;
            if (!file) {
                *status = &c_status_file_not_open;
            } else {
                res = file->write(&command->message[4], command->length - 4, &transferred);
                *status = &c_status_ok;
                if (res != FR_OK) {
                    fres_status(res, status);
                }
            }
            break;
        case DOS_CMD_ECHO:
            command->last_part = true;
            *reply = command;
            *status = &c_status_ok;
            break;
        case DOS_CMD_GET_TIME: {
            int y, M, D, wd, h, m, s;
            rtc.get_time(y, M, D, wd, h, m, s);
            int extFormat = command->length > 2 ? command->message[2] : 0;
            if (extFormat == 0) {
                sprintf((char *)data_message.message, "%4d/%02d/%02d %02d:%02d:%02d", 1980 + y, M, D, h, m, s);
                data_message.length = 19;
                *status = &c_status_ok;
                data_message.last_part = true;
                *reply = &data_message;
            } else if (extFormat == 1) {
                sprintf((char *)data_message.message, "%s %4d/%02d/%02d %02d:%02d:%02d", wdnames[wd], 1980 + y, M, D, h, m, s);
                data_message.length = 23;
                *status = &c_status_ok;
                data_message.last_part = true;
                *reply = &data_message;
            } else {
                *status = &c_status_unknown_command;
                *reply = &c_message_empty;
            }
            break;
        }
        case DOS_CMD_SET_TIME: {
            int y, M, D, wd, h, m, s, yz, mz, cz;
            int clen = command->length;
            if (clen != 8) {
                sprintf((char *)data_message.message, "%02d", clen);
                data_message.length = 2;
                data_message.last_part = true;
                *reply = &data_message;
                *status = &c_status_unknown_command;
            } else {
                y = command->message[2];
                M = command->message[3];
                D = command->message[4];
                h = command->message[5];
                m = command->message[6];
                s = command->message[7];
                yz = y + 1900;
                y -= 80;
                mz = M;
                if (M < 3) {
                    yz--;
                    mz += 12;
                }
                cz = yz / 100;
                yz = yz % 100;
                int wdz = D + (mz + 1) * 13 / 5 + yz + yz / 4 + cz / 4 - 2 * cz + 6;
                wdz = wdz % 7;
                if (wdz < 0) {
                    wdz += 7;
                }
                int corr = rtc.get_correction();
                rtc.set_time(y, M, D, wdz, h, m, s);
                rtc.set_time_in_chip(corr, y, M, D, wdz, h, m, s);
                rtc.get_time(y, M, D, wd, h, m, s);
                sprintf((char *)data_message.message, "%s %4d/%02d/%02d %02d:%02d:%02d", wdnames[wd], 1980 + y, M, D, h, m, s);
                data_message.length = 23;
                *status = &c_status_ok;
                data_message.last_part = true;
                *reply = &data_message;
            }
            break;
        }
        default:
            *reply = &c_message_empty;
            *status = &c_status_unknown_command;
        }
    }

    void get_more_data(Message **reply, Message **status)
    {
        uint32_t transferred = 0;
        FRESULT res;
        int length;
        FileInfo *fi;

        switch (dos_state) {
        case e_dos_idle:
            *reply = &c_message_empty;
            *status = &c_status_no_data;
            break;
        case e_dos_in_file:
            length = (remaining > 512) ? 512 : remaining;
            res = file->read(data_message.message, length, &transferred);
            data_message.length = (int)transferred;
            remaining -= transferred;
            if (((int)transferred != length) || (remaining == 0)) {
                data_message.last_part = true;
                dos_state = e_dos_idle;
            } else {
                data_message.last_part = false;
            }
            if (res != FR_OK) {
                strcpy((char *)status_message.message, FileSystem::get_error_string(res));
                status_message.length = strlen((char *)status_message.message);
            } else {
                *status = &c_message_empty;
            }
            *reply = &data_message;
            break;
        case e_dos_in_directory:
            fi = directoryList[current_index];
            if (!fi) {
                *status = &c_status_internal_error;
                *reply = &c_message_empty;
            } else {
                data_message.message[0] = fi->attrib;
                strcpy((char *)&data_message.message[1], fi->lfname);
                data_message.length = 1 + strlen((char *)&data_message.message[1]);
                current_index++;
                if (current_index == dir_entries) {
                    data_message.last_part = true;
                    *status = &c_status_ok;
                    dos_state = e_dos_idle;
                } else {
                    *status = &c_message_empty;
                    data_message.last_part = false;
                }
                *reply = &data_message;
            }
            break;
        default:
            dos_state = e_dos_idle;
        }
    }

    void abort(int) { dos_state = e_dos_idle; }
};

// ---- CONTROL (control_target.cc) -----------------------------------------------------------------------------------------------------------------------
#define CTRL_CMD_IDENTIFY       0x01
#define CTRL_CMD_FREEZE         0x05
#define CTRL_CMD_U64_SAVEMEM    0x0F
#define CTRL_CMD_DECODE_TRACK   0x11
#define CTRL_CMD_EASYFLASH      0x20
#define CTRL_CMD_LOAD_CONFIG    0x50
#define CTRL_CMD_GET_PALETTE    0x51
#define CTRL_CMD_SET_PALETTE    0x52
#define CTRL_CMD_SET_PALETTE_COLOR 0x53
#define CTRL_CMD_RESET_PALETTE  0x54
#define CTRL_CMD_REBOOT         0x06
#define CTRL_CMD_LOAD_REU       0x08
#define CTRL_CMD_SAVE_REU       0x09
#define CTRL_CMD_GET_HWINFO     0x28
#define CTRL_CMD_GET_DRVINFO    0x29
#define CTRL_CMD_ENABLE_DISK_A  0x30
#define CTRL_CMD_DISABLE_DISK_A 0x31
#define CTRL_CMD_ENABLE_DISK_B  0x32
#define CTRL_CMD_DISABLE_DISK_B 0x33
#define CTRL_CMD_DISK_A_POWER   0x34
#define CTRL_CMD_DISK_B_POWER   0x35
#define CTRL_CMD_GET_RAMDISKINFO 0x40

static Message c_message_identification = { 19, true, (uint8_t *)"CONTROL TARGET V1.1" };
static Message c_status_invalid_params  = { 17, true, (uint8_t *)"81,INVALID PARAMS" };
static Message c_status_errors_on_track = { 18, true, (uint8_t *)"82,ERRORS ON TRACK" };
static Message c_status_no_data_ctrl    = { 19, true, (uint8_t *)"83,NOT IN DATA MODE" };
static Message c_status_reu_disabled    = { 18, true, (uint8_t *)"84,REU NOT ENABLED" };
static Message c_status_cannot_open_file = { 28, true, (uint8_t *)"85,REU FILE CANNOT BE OPENED" };
static Message c_status_reu_not_saved   = { 31, true, (uint8_t *)"86,REU OFFSET > SIZE. NOT SAVED" };
static Message c_status_cannot_open_config = { 26, true, (uint8_t *)"88,CANNOT OPEN CONFIG FILE" };
static Message c_status_config_had_errors  = { 25, true, (uint8_t *)"89,CONFIG FILE HAD ERRORS" };

static volatile bool g_freeze_request;
bool uci_take_freeze_request(void)
{
    bool r = g_freeze_request;
    g_freeze_request = false;
    return r;
}

class ControlTarget : CommandTarget
{
    Message data_message;
    Message status_message;

    // REUPreloader::LoadREU / SaveREU (reu_preloader.cc): the preload image into the REU at the preload offset, or the REU from there into the image (the old image goes to .bu).
    int reu_xfer(bool save, char *status)
    {
        uint32_t size = 0, offset = 0, transferred = 0;
        char *img = new char[FS_PATH_MAX], *busy = new char[96];
        int r = g_host->reu ? g_host->reu(g_ctx, &size, &offset, img, FS_PATH_MAX, busy, 96) : -2;
        FileManager *fm = FileManager::getFileManager();
        FRESULT fres = FR_OK;
        File *f = 0;
        if (r == -4) {
            sprintf(status, "REU %s: %s", save ? "Save" : "Load", busy);
        } else if (r == 0 && !save) {
            fres = fm->fopen(img, FA_READ, &f);
            if (fres != FR_OK) {
                sprintf(status, "REU Load: Failed to open %s: %s", img, FileSystem::get_error_string(fres));
                r = -1;
            } else {
                if (offset < size) {
                    transferred = reu_from_file(f, offset, size - offset, &fres);
                    sprintf(status, "REU Load: Loaded %d bytes to $%6x from %s", (int)transferred, (unsigned)offset, img);
                } else {
                    sprintf(status, "REU Load: Offset %d >= REU size %d, skipping", (int)offset, (int)size);
                }
                fm->fclose(f);
                r = (int)transferred;
            }
        } else if (r == 0) {
            if (offset >= size) {
                sprintf(status, "REU Save: Offset %d >= REU size %d, skipping", (int)offset, (int)size);
                r = -3;
            } else if (fs_write_refused(img)) {
                sprintf(status, "REU Save: Failed to open %s: %s", img, FileSystem::get_error_string(FR_WRITE_PROTECTED));
                r = -1;
            } else {
                char *bu = new char[strlen(img) + 8];
                strcpy(bu, img);
                char *dot = strrchr(bu, '.');
                char *slash = strrchr(bu, '/');
                if (dot && (!slash || dot > slash)) {
                    *dot = 0;
                }
                strcat(bu, ".bu");
                fm->delete_file(bu);
                fm->rename(img, bu);
                fres = fm->fopen(img, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
                if (fres != FR_OK) {
                    sprintf(status, "REU Save: Failed to open %s: %s", img, FileSystem::get_error_string(fres));
                    r = -1;
                } else {
                    transferred = reu_to_file(f, offset, size - offset, &fres);
                    sprintf(status, "REU Save: Saved %d bytes from $%6x to %s", (int)transferred, (unsigned)offset, img);
                    fm->fclose(f);
                    r = (int)transferred;
                }
                delete[] bu;
            }
        }
        delete[] img;
        delete[] busy;
        return r;
    }

    void power(int idx, bool on, Message **status)
    {
        char msg[80];
        if (g_host->drive_do) {
            g_host->drive_do(g_ctx, idx, on ? 4 : 3, NULL, msg, sizeof(msg));
        }
        *status = &c_status_ok;
    }

    // control_target.cc:save_u64_memory: the 64 KB of the C64 through the DMA window in memory-only mode (so $D000-$DFFF is RAM) into a file. The message after the command is the name.
    void save_u64_memory(Message *command)
    {
        FileManager *fm = FileManager::getFileManager();
        File *f;
        char fn[160];
        snprintf(fn, sizeof(fn), "%s", command->length > 2 ? (const char *)command->message + 2 : (g_host->memory_file ? g_host->memory_file : "/temp/memory.bin"));
        FRESULT res = fs_write_refused(fn) ? FR_WRITE_PROTECTED : fm->fopen(fn, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
        if (res == FR_OK) {
            uint8_t *dest = new uint8_t[65536];
            g_ctx->io->ram_memonly(true);
            g_ctx->io->ram_read(0, dest, 65536);
            g_ctx->io->ram_memonly(false);
            uint32_t bytes_written;
            res = f->write(dest, 0x10000, &bytes_written);
            fm->fclose(f);
            delete[] dest;
        }
        if (res == FR_OK) {
            sprintf((char *)status_message.message, "00,OK");
        } else {
            sprintf((char *)status_message.message, "87,DISK ERR: %s", FileSystem::get_error_string(res));
        }
        status_message.length = strlen((char *)status_message.message);
    }

    // control_target.cc:decode_track: the GCR of a track in the REU (offset, 24 bits) decoded into 256-byte sectors in the REU, by the GCR decoder of the bitstream (iomap.h
    // GCR_CODER_BASE). The firmware passes the uninitialised `secs` as the length of the status area (control_target.cc:76); 2 * maxSector, what it is meant to be, is used here.
    static uint8_t *find_sync(uint8_t *gcr_data, uint8_t *begin, uint8_t *end)
    {
        bool wrap = false;
        int sync_count = 0;
        do {
            if (gcr_data >= end) {
                gcr_data = begin;
                if (wrap) {
                    return NULL;
                }
                wrap = true;
            }
            if (*gcr_data == 0xFF) {
                sync_count++;
            } else {
                if (sync_count >= 2) {
                    return gcr_data;
                }
                sync_count = 0;
            }
            gcr_data++;
        } while (1);
        return NULL;
    }
    static void conv_5bytes_gcr2bin(uint8_t **gcr, uint8_t *bin)
    {
        uint8_t *b = *gcr;
        *(volatile uint8_t *)(GCR_CODER_BASE + 0x00) = *(b++);
        *(volatile uint8_t *)(GCR_CODER_BASE + 0x00) = *(b++);
        *(volatile uint8_t *)(GCR_CODER_BASE + 0x00) = *(b++);
        *(volatile uint8_t *)(GCR_CODER_BASE + 0x00) = *(b++);
        *(volatile uint8_t *)(GCR_CODER_BASE + 0x00) = *(b++);
        *(bin++) = *(volatile uint8_t *)(GCR_CODER_BASE + 0x00);
        *(bin++) = *(volatile uint8_t *)(GCR_CODER_BASE + 0x01);
        *(bin++) = *(volatile uint8_t *)(GCR_CODER_BASE + 0x02);
        *(bin++) = *(volatile uint8_t *)(GCR_CODER_BASE + 0x03);
        *gcr = b;
    }
    static uint8_t *wrap_bits(uint8_t **current, uint8_t *begin, uint8_t *end, int count, uint8_t *buffer, uint8_t shift)
    {
        uint8_t *gcr = *current;
        if (shift == 0) {
            if (gcr > (end - count)) {
                uint8_t *d = buffer;
                uint8_t *s = gcr;
                *current = (gcr + count) - (end - begin);
                while (count--) {
                    if (s == end) {
                        s = begin;
                    }
                    *(d++) = *(s++);
                }
                return buffer;
            }
            *current = gcr + count;
            return gcr;
        }
        uint8_t *d = buffer;
        uint8_t *s = gcr;
        uint8_t curByte = *(s++) << shift;
        if (gcr > (end - count)) {
            *current = (gcr + count) - (end - begin);
        } else {
            *current = gcr + count;
        }
        while (count--) {
            if (s == end) {
                s = begin;
            }
            curByte |= *s >> (8 - shift);
            *(d++) = curByte;
            curByte = *s << shift;
            s++;
        }
        return buffer;
    }
    int gcr_track_to_bin(uint8_t *gcr, int trackNumber, int trackLen, int maxSector, uint8_t *bin, uint8_t *status, int statlen)
    {
        static uint8_t header[8];
        int t, s;
        uint8_t *begin = gcr, *end = begin + trackLen, *current = gcr, *pntr, *dest = bin, *gcr_data, *new_gcr, *st = status;
        bool expect_data = false, wrapped = false;
        int secs = 0;
        uint8_t sector_buffer[352];
        while (secs < maxSector) {
            new_gcr = find_sync(current, begin, end);
            if (!new_gcr) {
                break;
            }
            if (new_gcr < current) {
                if (wrapped) {
                    break;
                }
                wrapped = true;
            }
            pntr = current = new_gcr;
            uint8_t shift = 0;
            uint8_t firstByte = *new_gcr;
            while (firstByte & 0x80) {
                shift++;
                firstByte <<= 1;
                if (shift == 8) {
                    break;
                }
            }
            gcr_data = wrap_bits(&pntr, begin, end, 5, sector_buffer, shift);
            conv_5bytes_gcr2bin(&gcr_data, &header[0]);
            if (header[0] == 8) {
                gcr_data = wrap_bits(&pntr, begin, end, 5, sector_buffer, shift);
                conv_5bytes_gcr2bin(&gcr_data, &header[4]);
                t = (int)header[3];
                s = (int)header[2];
                dest = bin + (256 * s);
                if (expect_data && st && statlen > 0) {
                    *(st++) = 0xE4;
                    statlen--;
                }
                if (st && statlen > 0) {
                    *(st++) = s;
                    statlen--;
                }
                expect_data = true;
                if (t != trackNumber) {
                    if (st && statlen > 0) {
                        *(st++) = 0xE1;
                        statlen--;
                        expect_data = false;
                    }
                } else if (s >= maxSector) {
                    if (st && statlen > 0) {
                        *(st++) = 0xE2;
                        statlen--;
                        expect_data = false;
                    }
                }
                if (expect_data) {
                    current = pntr;
                }
                continue;
            }
            if (header[0] == 7 && expect_data) {
                expect_data = false;
                secs++;
                uint8_t *binarySector = dest;
                memcpy(dest, &header[1], 3);
                dest += 3;
                gcr_data = wrap_bits(&pntr, begin, end, 320, sector_buffer, shift);
                for (int i = 0; i < 63; i++) {
                    conv_5bytes_gcr2bin(&gcr_data, dest);
                    dest += 4;
                }
                conv_5bytes_gcr2bin(&gcr_data, &header[4]);
                *(dest++) = header[4];
                if (st && statlen > 0) {
                    uint8_t chk = 0;
                    for (int i = 0; i < 256; i++) {
                        chk ^= *(binarySector++);
                    }
                    if (chk != header[5]) {
                        *(st++) = 0xE3;
                    } else {
                        *(st++) = 0x00;
                        current = pntr;
                    }
                    statlen--;
                }
            }
        }
        return secs;
    }
    void decode_track(Message *command, Message **reply, Message **status)
    {
        if (command->length != 14) {
            *reply = &c_message_empty;
            *status = &c_status_invalid_params;
            return;
        }
        int track = command->message[2];
        int maxSector = command->message[3];
        uint32_t gcrAddress = (uint32_t)command->message[4] | ((uint32_t)command->message[5] << 8) | ((uint32_t)command->message[6] << 16);
        uint32_t binAddress = (uint32_t)command->message[8] | ((uint32_t)command->message[9] << 8) | ((uint32_t)command->message[10] << 16);
        uint16_t trackLen = (uint16_t)command->message[12] | ((uint16_t)command->message[13] << 8);
        if (!trackLen || (gcrAddress + trackLen) > UCI_REU_BYTES || (binAddress + 256u * (uint32_t)maxSector) > UCI_REU_BYTES) {
            *reply = &c_message_empty;                                      // the firmware would work on whatever lies there; the REU ends at 16 MB
            *status = &c_status_invalid_params;
            return;
        }
        uint8_t *gcr = new uint8_t[trackLen];
        uint8_t *bin = new uint8_t[256u * maxSector + 1];
        g_ctx->io->ddr_read(gcrAddress, gcr, trackLen);
        g_ctx->io->ddr_read(binAddress, bin, 256u * maxSector);
        int secs = gcr_track_to_bin(gcr, track, trackLen, maxSector, bin, data_message.message + 1, 2 * maxSector);
        g_ctx->io->ddr_write(binAddress, bin, 256u * maxSector);
        delete[] gcr;
        delete[] bin;
        data_message.message[0] = (uint8_t)secs;
        data_message.length = 1 + 2 * secs;
        data_message.last_part = true;
        *reply = &data_message;
        *status = &c_status_ok;
        if (secs == maxSector) {
            uint8_t *code = data_message.message + 2;
            for (int i = 0; i < secs; i++) {
                if (*code) {
                    *status = &c_status_errors_on_track;
                    break;
                }
                code += 2;
            }
        } else {
            *status = &c_status_errors_on_track;
        }
    }

    // control_target.cc:load_config: the file is read here, the operation applies it (settings_ops.cc); the reply is the log of the lines that could not be applied.
    void load_config(Message *command, Message **reply, Message **status)
    {
        FileManager *fm = FileManager::getFileManager();
        File *f;
        const char *fn = "/temp/uci_config.cfg";
        if (command->length > 2) {
            fn = (const char *)command->message + 2;
        }
        FRESULT res = fm->fopen(fn, FA_READ, &f);
        if (res != FR_OK) {
            *reply = &c_message_empty;
            *status = &c_status_cannot_open_config;
            return;
        }
        uint8_t *text = new uint8_t[16384];
        uint32_t got = 0, tot = 0;
        while (tot < 16384 && f->read(text + tot, 16384 - tot, &got) == FR_OK && got) {
            tot += got;
        }
        fm->fclose(f);
        settings_stage((const char *)text, (int)tot);
        delete[] text;
        OpResult *r = new OpResult;
        int rc = ops_call(g_ctx, "settings.load_staged", NULL, 0, r);
        delete r;
        int loglen = (int)strlen(settings_load_log());
        if (loglen > 511) {
            loglen = 511;
        }
        memcpy(data_message.message, settings_load_log(), loglen);
        data_message.length = loglen;
        data_message.last_part = true;
        *reply = &data_message;
        *status = rc == OPE_OK ? &c_status_ok : &c_status_config_had_errors;
    }

public:
    ControlTarget(int id)
    {
        command_targets[id] = this;
        data_message.message = new uint8_t[512];
        status_message.message = new uint8_t[80];
    }
    ~ControlTarget()
    {
        delete[] data_message.message;
        delete[] status_message.message;
    }

    void parse_command(Message *command, Message **reply, Message **status)
    {
        int retVal;
        *reply = &c_message_empty;
        *status = &c_status_unknown_command;

        switch (command->message[1]) {
        case CTRL_CMD_IDENTIFY:
            *reply = &c_message_identification;
            *status = &c_status_ok;
            break;
        case CTRL_CMD_REBOOT: {
            OpResult *r = new OpResult;
            ops_call(g_ctx, "machine.cold_reset", NULL, 0, r);
            delete r;
            *status = &c_message_empty;                                     // control_target.cc does the same
            break;
        }
        case CTRL_CMD_FREEZE:
            g_freeze_request = true;                                        // the run loop opens the menu as the menu button does (run.cc)
            *status = &c_message_empty;                                     // control_target.cc does the same
            break;
        case CTRL_CMD_U64_SAVEMEM:
            *reply = &c_message_empty;
            *status = &status_message;
            save_u64_memory(command);
            break;
        case CTRL_CMD_DECODE_TRACK:
            decode_track(command, reply, status);
            break;
        case CTRL_CMD_GET_PALETTE:
            if (command->length != 2) {
                *status = &c_status_invalid_params;
            } else if (g_host->palette) {
                g_host->palette(g_ctx, 0, 0, (uint8_t (*)[3])data_message.message);
                data_message.length = UCI_PALETTE_BYTES;
                data_message.last_part = true;
                *reply = &data_message;
                *status = &c_status_ok;
            }
            break;
        case CTRL_CMD_SET_PALETTE: {
            uint8_t rgb[UCI_PALETTE_COLORS][3];
            if (!decode_palette_set(command->message, command->length, rgb)) {
                *status = &c_status_invalid_params;
            } else if (g_host->palette) {
                g_host->palette(g_ctx, 1, 0, rgb);
                *status = &c_status_ok;
            }
            break;
        }
        case CTRL_CMD_SET_PALETTE_COLOR: {
            uint8_t index;
            uint8_t rgb[3];
            if (!decode_palette_color_set(command->message, command->length, &index, rgb)) {
                *status = &c_status_invalid_params;
            } else if (g_host->palette) {
                uint8_t one[UCI_PALETTE_COLORS][3];
                memcpy(one[0], rgb, 3);
                g_host->palette(g_ctx, 2, index, one);
                *status = &c_status_ok;
            }
            break;
        }
        case CTRL_CMD_RESET_PALETTE:
            if (!valid_palette_reset(command->length)) {
                *status = &c_status_invalid_params;
            } else if (g_host->palette) {
                g_host->palette(g_ctx, 3, 0, NULL);
                *status = &c_status_ok;
            }
            break;
        case CTRL_CMD_EASYFLASH:
            if (command->length < 3) {
                *status = &c_status_invalid_params;
                *reply = &c_message_empty;
            } else if (command->message[2] == 0) {
                if (command->length < 5) {
                    *status = &c_status_invalid_params;
                    *reply = &c_message_empty;
                    break;
                }
                if (g_host->ef_clear) {
                    g_host->ef_clear(g_ctx, command->message[3] & 0x38, (command->message[4] & 0x20) != 0);
                }
                *reply = &c_message_empty;
                *status = &c_status_ok;
            } else {
                *reply = &c_message_empty;
                *status = &c_status_invalid_params;
            }
            break;
        case CTRL_CMD_LOAD_CONFIG:
            load_config(command, reply, status);
            break;
        case CTRL_CMD_ENABLE_DISK_A:
            power(0, true, status);
            break;
        case CTRL_CMD_DISABLE_DISK_A:
            power(0, false, status);
            break;
        case CTRL_CMD_ENABLE_DISK_B:
            power(1, true, status);
            break;
        case CTRL_CMD_DISABLE_DISK_B:
            power(1, false, status);
            break;
        case CTRL_CMD_DISK_A_POWER:
        case CTRL_CMD_DISK_B_POWER: {
            uint8_t type, addr;
            bool on = false;
            if (g_host->drive_info) {
                g_host->drive_info(g_ctx, command->message[1] == CTRL_CMD_DISK_A_POWER ? 0 : 1, &type, &addr, &on);
            }
            sprintf((char *)data_message.message, on ? "on " : "off");
            data_message.length = 3;
            *status = &c_status_ok;
            data_message.last_part = true;
            *reply = &data_message;
            break;
        }
        case CTRL_CMD_GET_DRVINFO: {
            data_message.length = 1;
            data_message.message[0] = 0;
            data_message.last_part = true;
            *status = &c_status_ok;
            *reply = &data_message;
            int offs = 1;
            for (int i = 0; i < 2; i++) {
                uint8_t type, addr;
                bool on;
                if (g_host->drive_info && g_host->drive_info(g_ctx, i, &type, &addr, &on)) {
                    data_message.message[0]++;
                    data_message.message[offs++] = type;
                    data_message.message[offs++] = addr;
                    data_message.message[offs++] = on ? 1 : 0;
                    data_message.length += 3;
                }
            }
            break;
        }
        case CTRL_CMD_LOAD_REU:
        case CTRL_CMD_SAVE_REU:
            *status = &c_status_ok;
            *reply = &c_message_empty;
            if (command->length >= 5) {
                *reply = &data_message;
                data_message.message[4] = 0;
                retVal = reu_xfer(command->message[1] == CTRL_CMD_SAVE_REU, (char *)data_message.message + 4);
                uint32_t v = cpu_to_32le((uint32_t)retVal);
                memcpy(data_message.message, &v, 4);
                data_message.last_part = true;
                for (char *p = (char *)data_message.message + 4; *p; p++) {
                    if (*p >= 'a' && *p <= 'z') {
                        *p -= 32;
                    }
                }
                data_message.length = 4 + strlen((char *)data_message.message + 4);
            } else {
                retVal = -4;
            }
            if (retVal == -4) {
                *status = &c_status_invalid_params;
            } else if (retVal == -3) {
                *status = &c_status_reu_not_saved;
            } else if (retVal == -2) {
                *status = &c_status_reu_disabled;
            } else if (retVal == -1) {
                *status = &c_status_cannot_open_file;
            }
            break;
        case CTRL_CMD_GET_HWINFO: {
            int device = command->length < 3 ? 0 : command->message[2];
            if (device == 0) {
                strcpy((char *)data_message.message, getProductString());
                data_message.length = strlen((char *)data_message.message);
                *status = &c_status_ok;
                data_message.last_part = true;
                *reply = &data_message;
            } else if (device == 1 && g_host->sid_map) {
                data_message.length = g_host->sid_map(g_ctx, data_message.message);
                *status = &c_status_ok;
                data_message.last_part = true;
                *reply = &data_message;
            } else {
                *reply = &c_message_empty;
                *status = &c_status_invalid_params;
            }
            break;
        }
        case CTRL_CMD_GET_RAMDISKINFO: {
            data_message.length = 8;
            unsigned char *data = (unsigned char *)data_message.message;
            for (int i = 0; i < 4; i++) {
                int typ = 0;
                uint32_t size = 0, base = 0;
                if (g_host->ramdrive) {
                    g_host->ramdrive(g_ctx, i, &typ, &size, &base);
                }
                unsigned char ty = 0, si = 0;
                if (typ == 1541) {
                    ty = 0x41;
                } else if (typ == 1571) {
                    ty = 0x71;
                } else if (typ == 1581) {
                    ty = 0x81;
                } else if (typ == DRVTYPE_MP3_DNP) {
                    ty = 0xDD;
                    si = (unsigned char)(size >> 16);
                }
                data[2 * i] = ty;
                data[2 * i + 1] = si;
            }
            *status = &c_status_ok;
            data_message.last_part = true;
            *reply = &data_message;
            break;
        }
        }
    }
    void get_more_data(Message **reply, Message **status)
    {
        *reply = &c_message_empty;
        *status = &c_status_no_data_ctrl;
    }
    void abort(int) { }
};

// Not answered (unknown command, 21 - or, for target 5 and 6, no such target): CONTROL FINISH_CAPTURE 0x03 (the application has no tape recorder, though the bitstream has the
// recorder), the SoftIEC target 5 (the bitstream has no software IEC processor) and the HTTP target 6 (not built: it needs the firmware's HTTP client and request streams).
// The firmware has no case for CONTROL READ_RTC 0x02 and ENCODE_TRACK 0x12 either.

void uci_start(CoreCtx *c, const UciHost *host)
{
    static bool started;
    uint32_t caps = getFpgaCapabilities();
    if (started || !(caps & CAPAB_COMMAND_INTF) || !(caps & CAPAB_CARTRIDGE)) {
        return;
    }
    started = true;
    g_ctx = c;
    g_host = host;
    new Dos(1);
    new Dos(2);
    new NetworkTarget(3);
    new ControlTarget(4);
    cmd_if.start();
}
