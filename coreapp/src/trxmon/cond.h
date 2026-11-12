/*
 * cond.h — the observer condition (TRX64 observers.rs: parse_cond / eval_node) and how much of it a watch unit
 * can hold (T21-monitor.md §4.4.1, T55).
 *
 * Grammar: terms a x y pc sp fl rl val addr cy, hex numbers (bare, or with a leading $; `a` the register, `$a` the number) and %bin; == != < > <= >= && || and ( ). No libc, no
 * allocation: a condition is at most COND_NODES nodes.
 */
#ifndef TRXMON_COND_H
#define TRXMON_COND_H

#include <stdint.h>

#define COND_NODES 24
#define COND_REST  8 /* conjuncts left for software */

enum { CN_NUM = 0, CN_ID, CN_BIN };
enum { CI_A = 0, CI_X, CI_Y, CI_PC, CI_SP, CI_FL, CI_RL, CI_VAL, CI_ADDR, CI_CY };
enum { CO_EQ = 0, CO_NE, CO_LT, CO_GT, CO_LE, CO_GE, CO_AND, CO_OR };

typedef struct
{
    uint8_t kind;  /* CN_* */
    uint8_t op;    /* CN_ID: CI_*, CN_BIN: CO_* */
    int8_t l, r;   /* children of a CN_BIN */
    int32_t v;     /* CN_NUM */
} cnode_t;

typedef struct
{
    cnode_t n[COND_NODES];
    int8_t root;   /* -1: no condition */
    uint8_t count;
} cond_t;

/* What an observer's condition is evaluated against */
typedef struct
{
    int64_t a, x, y, pc, sp, fl, rl, val, addr, cy;
} cond_env_t;

/* NULL, or the reason src does not parse. An empty src is no condition (root -1). */
const char *cond_parse(const char *src, cond_t *c);
int64_t cond_eval(const cond_t *c, int node, const cond_env_t *e);

/* What a watch unit holds of a condition on a load/store observer: the address range narrowed by `addr`
 * comparisons, DATA (val == v), one REG qualifier (a/x/y/sp against a number). The rest is software: the
 * conjuncts (nodes of c) that must all be non-zero after the stop. A condition that is not a conjunction is all
 * rest. With `units` = 0 (exec observers: the bitmap holds the address only) everything is rest. */
typedef struct
{
    uint16_t lo, hi;
    uint8_t has_data, dmask, dval;
    uint32_t reg; /* UNIT_REG(...) or 0 */
    int8_t rest[COND_REST];
    uint8_t nrest;
} cond_hw_t;

/* A node as text ("a>x"), nested comparisons in parentheses */
void cond_fmt(const cond_t *c, int node, char *out, int cap);
void cond_split(const cond_t *c, int units, uint16_t lo, uint16_t hi, cond_hw_t *q);
#endif
