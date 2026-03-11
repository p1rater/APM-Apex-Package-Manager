/*
 * version.c - RPM-style version comparison
 *
 * The comparison algorithm works character by character on "segments"
 * where a segment is a maximal run of digits or a maximal run of
 * letters. Digit segments are compared numerically, letter segments
 * lexically. Tildes sort before everything else (used for pre-release
 * suffixes: "1.0~rc1" < "1.0").
 *
 * Format: [epoch:]version[-release]
 *   epoch   — non-negative integer, defaults to 0
 *   version — alphanumeric with dots and underscores
 *   release — everything after the last hyphen
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "apex/version.h"
#include "apex/util.h"

/* ── Segment comparison ──────────────────────────────────────────────────── */

/*
 * Compare one segment: returns <0, 0, >0
 * Both a and b point to the start of a segment; end1/end2 are one
 * past the segment. Numeric vs alpha is already known from is_num.
 */
static int cmp_seg(const char *a, size_t alen, bool a_num,
                   const char *b, size_t blen, bool b_num)
{
    /* a numeric segment is always greater than an alpha segment */
    if (a_num != b_num)
        return a_num ? 1 : -1;

    if (a_num) {
        /* numeric: skip leading zeros, then compare by length then value */
        while (alen > 1 && *a == '0') { a++; alen--; }
        while (blen > 1 && *b == '0') { b++; blen--; }
        if (alen != blen) return (alen > blen) ? 1 : -1;
        return strncmp(a, b, alen);
    } else {
        /* alpha: lexicographic */
        int r = strncmp(a, b, alen < blen ? alen : blen);
        if (r) return r;
        return (alen > blen) ? 1 : (alen < blen) ? -1 : 0;
    }
}

/*
 * Core version comparison, no epoch, no release.
 */
static int cmp_version(const char *a, const char *b)
{
    while (*a || *b) {
        /* tildes sort lowest */
        if (*a == '~' || *b == '~') {
            if (*a != '~') return  1;
            if (*b != '~') return -1;
            a++; b++;
            continue;
        }

        /* skip non-alphanumeric non-tilde */
        while (*a && !isalnum((unsigned char)*a) && *a != '~') a++;
        while (*b && !isalnum((unsigned char)*b) && *b != '~') b++;

        if (!*a && !*b) break;
        if (!*a) return -1;
        if (!*b) return  1;

        /* identify segment type */
        bool a_num = isdigit((unsigned char)*a);
        bool b_num = isdigit((unsigned char)*b);

        /* measure segment length */
        const char *as = a, *bs = b;
        if (a_num) while (*a &&  isdigit((unsigned char)*a)) a++;
        else       while (*a && !isdigit((unsigned char)*a) &&
                                *a != '~' &&
                                isalnum((unsigned char)*a)) a++;
        if (b_num) while (*b &&  isdigit((unsigned char)*b)) b++;
        else       while (*b && !isdigit((unsigned char)*b) &&
                                *b != '~' &&
                                isalnum((unsigned char)*b)) b++;

        int r = cmp_seg(as, (size_t)(a - as), a_num,
                        bs, (size_t)(b - bs), b_num);
        if (r) return r;
    }
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int apex_ver_parse(const char *str, apex_ver_parts_t *out)
{
    if (!str || !out) return -1;
    memset(out, 0, sizeof(*out));

    const char *p = str;

    /* epoch: digits before first colon */
    const char *colon = strchr(p, ':');
    if (colon) {
        char *end;
        long epoch = strtol(p, &end, 10);
        if (end == colon && epoch >= 0) {
            out->epoch = (unsigned int)epoch;
            p = colon + 1;
        }
    }

    /* release: after last hyphen */
    const char *hyphen = strrchr(p, '-');
    if (hyphen && hyphen != p) {
        out->version = apex_strndup(p, (size_t)(hyphen - p));
        out->release = apex_strdup(hyphen + 1);
    } else {
        out->version = apex_strdup(p);
        out->release = NULL;
    }

    return 0;
}

void apex_ver_parts_free(apex_ver_parts_t *p)
{
    if (!p) return;
    apex_free(p->version);
    apex_free(p->release);
}

int apex_ver_cmp(const char *a, const char *b)
{
    if (!a && !b) return 0;
    if (!a)       return -1;
    if (!b)       return  1;

    apex_ver_parts_t pa, pb;
    apex_ver_parse(a, &pa);
    apex_ver_parse(b, &pb);

    int r = 0;

    /* 1. epoch */
    if (pa.epoch != pb.epoch) {
        r = (pa.epoch > pb.epoch) ? 1 : -1;
        goto done;
    }

    /* 2. version */
    r = cmp_version(pa.version, pb.version);
    if (r) goto done;

    /* 3. release */
    if (!pa.release && !pb.release) goto done;
    if (!pa.release)  { r = -1; goto done; }
    if (!pb.release)  { r =  1; goto done; }
    r = cmp_version(pa.release, pb.release);

done:
    apex_ver_parts_free(&pa);
    apex_ver_parts_free(&pb);
    return r;
}

bool apex_ver_valid(const char *ver)
{
    if (!ver || !*ver) return false;

    /* allow epoch prefix */
    const char *p = ver;
    if (isdigit((unsigned char)*p)) {
        const char *c = strchr(p, ':');
        if (c) p = c + 1;
    }

    /* must start with alnum */
    if (!isalnum((unsigned char)*p)) return false;

    for ( ; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!isalnum(c) && c != '.' && c != '_' && c != '+' &&
            c != '-' && c != '~')
            return false;
    }
    return true;
}

bool apex_ver_satisfies(const char *ver, dep_op_t op, const char *ref)
{
    if (op == DEP_ANY) return true;

    int r = apex_ver_cmp(ver, ref);

    switch (op) {
    case DEP_EQ: return r == 0;
    case DEP_GE: return r >= 0;
    case DEP_LE: return r <= 0;
    case DEP_GT: return r >  0;
    case DEP_LT: return r <  0;
    default:     return false;
    }
}

const char *apex_dep_op_str(dep_op_t op)
{
    switch (op) {
    case DEP_ANY: return "";
    case DEP_EQ:  return "==";
    case DEP_GE:  return ">=";
    case DEP_LE:  return "<=";
    case DEP_GT:  return ">";
    case DEP_LT:  return "<";
    default:      return "?";
    }
}
