/*
 * filetypes.h
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#ifndef FILEMANAGER_FILETYPES_H_
#define FILEMANAGER_FILETYPES_H_

#include "factory.h"
#include "action.h"
#include "file_info.h"
class BrowsableDirEntry;
class Browsable;

class FileType
{
public:
	FileType() { }
	virtual ~FileType() { }

	static Factory<BrowsableDirEntry *, FileType *>* getFileTypeFactory() {
		static Factory<BrowsableDirEntry *, FileType *> file_type_factory;
		return &file_type_factory;
	}

	virtual int fetch_context_items(IndexedList<Action *> &list) {
		return 0;
	}

	virtual int getCustomBrowsables(Browsable *parent, IndexedList<Browsable *> &list) {
	    return -1;
	}
};

// Smart mount (doc/specs/SPEC-05): the directory the last disk was run or
// mounted from on drive A. A disk menu opened in that same directory offers
// "Mount Disk" first, because the next disk of a set is swapped in, not run.
class Path;
class SubsysCommand;
bool smart_mount_same_dir(Path *dir);
void smart_mount_remember(const char *dir);
// "Mount Disk" on drive A through here, so that the mount is remembered
SubsysResultCode_e smart_mount_disk_st(SubsysCommand *cmd);

#endif /* FILEMANAGER_FILETYPES_H_ */
