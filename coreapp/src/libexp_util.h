// What the export files of the libraries (*_exports.cc) share.
#ifndef COREAPP_LIBEXP_UTIL_H
#define COREAPP_LIBEXP_UTIL_H

// a call that may be made with no place for the reason gets one of its own
#define REASON(msg, msglen) char loc_[160]; if (!(msg) || (msglen) < 1) { (msg) = loc_; (msglen) = (int)sizeof(loc_); }

#endif
