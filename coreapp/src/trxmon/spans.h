/*
 * spans.h — WHERE a monitor reply printed each address (TRX64 Spec 804,
 * crates/trx64-monitor/src/addr_spans.rs). A formatter wraps an address it
 * prints in a mark; monitor/exec strips the marks at its one exit and turns
 * them into `spans: [{line, start, end, addr, space, role, lens?, len?}]`.
 */
#ifndef TRXMON_SPANS_H
#define TRXMON_SPANS_H

#include "json.h"

#define SPAN_START '\x01'
#define SPAN_SEP   '\x02'
#define SPAN_END   '\x03'

enum { ROLE_PC = 0, ROLE_TARGET, ROLE_OPERAND, ROLE_MEMORY };

/* Prints `visible` marked as address addr (space c64). lens NULL or "cpu":
 * none. len: bytes a range span covers (a dump row), 1 for a point. */
void span_mark(sbuf_t *b, const char *visible, int vlen, uint16_t addr, int role, const char *lens, int len);
/* "$xxxx", marked */
void span_addr4(sbuf_t *b, uint16_t addr, int role);

/* Removes the marks from text (in place; returns the new length) and writes
 * the spans array as the value of the current key. */
int span_strip(char *text, int len, jw_t *w);

#endif
