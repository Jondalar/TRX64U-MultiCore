#!/usr/bin/env python3
"""T128 spec section 5: checks a folder-disk image (dumped by test_folderhd with FOLDERHD_DUMP=<file>) with amitools, an implementation of the
Amiga formats that shares nothing with coreapp/src/amiga_ffs.cc nor with the bench's reader.  pip install amitools.
   folderhd_amitools.py image [part]   -> prints the listing, exits 1 on a validator error."""
import sys
from amitools.fs.blkdev.RawBlockDevice import RawBlockDevice
from amitools.fs.rdb.RDisk import RDisk
from amitools.fs.ADFSVolume import ADFSVolume
from amitools.fs.validate.Validator import Validator
from amitools.fs.validate.Log import Log

img = sys.argv[1]
name = sys.argv[2] if len(sys.argv) > 2 else "DH0"
raw = RawBlockDevice(img, True)
raw.open()
rd = RDisk(raw)
rd.open()
part = rd.find_partition_by_drive_name(name)
dev = part.create_blkdev()
dev.open()
vol = ADFSVolume(dev)
vol.open()
rows = []


def walk(d, prefix):
    for e in d.get_entries():
        n = prefix + e.name.get_unicode_name()
        if e.is_dir():
            rows.append((n + "/", 0))
            e.read()
            walk(e, n + "/")
        else:
            rows.append((n, e.get_size()))


vol.root_dir.read()
walk(vol.root_dir, "")
for n, s in sorted(rows):
    print("%10d %s" % (s, n))
v = Validator(dev, Log.INFO)
v.scan_boot()
if not v.scan_root():
    print("validator: root block not found")
    sys.exit(1)
v.scan_dir_tree()
v.scan_files()
v.scan_bitmap()
errors, warns = v.get_summary()
for e in v.log.entries:
    if e.level >= Log.WARN:
        print(e)
print("validator: %d errors, %d warnings" % (errors, warns))
vol.close()
sys.exit(1 if errors else 0)
