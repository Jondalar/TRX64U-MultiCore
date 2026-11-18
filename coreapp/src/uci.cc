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
                image_type = 1571;
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
static Message c_status_reu_disabled    = { 18, true, (uint8_t *)"84,REU NOT ENABLED" };
static Message c_status_cannot_open_file = { 28, true, (uint8_t *)"85,REU FILE CANNOT BE OPENED" };
static Message c_status_reu_not_saved   = { 31, true, (uint8_t *)"86,REU OFFSET > SIZE. NOT SAVED" };

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
        case CTRL_CMD_GET_HWINFO:
            if (command->length < 3 || command->message[2] == 0) {
                strcpy((char *)data_message.message, getProductString());
                data_message.length = strlen((char *)data_message.message);
                *status = &c_status_ok;
                data_message.last_part = true;
                *reply = &data_message;
            }
            break;
        case CTRL_CMD_GET_RAMDISKINFO:                                      // no MegaPatch RAM drive in this machine: the firmware answers eight zero bytes then
            data_message.length = 8;
            memset(data_message.message, 0, 8);
            *status = &c_status_ok;
            data_message.last_part = true;
            *reply = &data_message;
            break;
        }
    }
    void get_more_data(Message **reply, Message **status)
    {
        *reply = &c_message_empty;
        *status = &c_status_unknown_command;
    }
    void abort(int) { }
};

// Not answered (unknown command, 21): DOS 0x41/0x42 RAM disk load/save and CONTROL 0x40 for a real RAM drive (the machine has none), CONTROL FREEZE 0x05, FINISH_CAPTURE 0x03,
// U64_SAVEMEM 0x0F, DECODE/ENCODE_TRACK 0x11/0x12, EASYFLASH 0x20, HWINFO device 1 (SID map), LOAD_CONFIG 0x50, the palette commands 0x51-0x54; the SoftIEC target 5 and the
// HTTP target 6 are not built.

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
