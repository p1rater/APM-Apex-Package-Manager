/*
 * test_version.c - unit tests for the version comparison engine
 *
 * Run with: ./test_version
 * All tests must pass (exit 0) before any release.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apex/version.h"

static int pass = 0, fail = 0;

static void check_cmp(const char *a, const char *b, int expected)
{
    int got = apex_ver_cmp(a, b);
    /* normalise to -1/0/1 */
    int norm = (got < 0) ? -1 : (got > 0) ? 1 : 0;
    int exp  = (expected < 0) ? -1 : (expected > 0) ? 1 : 0;

    if (norm == exp) {
        printf("  PASS  cmp(%s, %s) = %d\n", a, b, expected);
        pass++;
    } else {
        printf("  FAIL  cmp(%s, %s): expected %d, got %d\n",
               a, b, expected, norm);
        fail++;
    }
}

static void check_sat(const char *ver, dep_op_t op,
                      const char *ref, bool expected)
{
    bool got = apex_ver_satisfies(ver, op, ref);
    if (got == expected) {
        printf("  PASS  %s %s %s\n", ver, apex_dep_op_str(op), ref);
        pass++;
    } else {
        printf("  FAIL  %s %s %s: expected %s, got %s\n",
               ver, apex_dep_op_str(op), ref,
               expected ? "true" : "false",
               got      ? "true" : "false");
        fail++;
    }
}

static void check_valid(const char *ver, bool expected)
{
    bool got = apex_ver_valid(ver);
    if (got == expected) {
        printf("  PASS  valid(%s) = %s\n", ver, expected ? "true" : "false");
        pass++;
    } else {
        printf("  FAIL  valid(%s): expected %s, got %s\n",
               ver, expected ? "true" : "false", got ? "true" : "false");
        fail++;
    }
}

int main(void)
{
    printf("\n  apex version comparison tests\n");
    printf("  ──────────────────────────────────\n\n");

    /* basic ordering */
    check_cmp("1.0",   "1.0",    0);
    check_cmp("1.0",   "2.0",   -1);
    check_cmp("2.0",   "1.0",    1);
    check_cmp("1.0.1", "1.0.0",  1);
    check_cmp("1.0.0", "1.0.1", -1);

    /* numeric vs alpha segments */
    check_cmp("10",  "9",    1);   /* numeric: 10 > 9, not "1" < "9" */
    check_cmp("9",   "10",  -1);
    check_cmp("100", "99",   1);

    /* pre-release tilde */
    check_cmp("1.0~rc1", "1.0",     -1);
    check_cmp("1.0",     "1.0~rc1",  1);
    check_cmp("1.0~rc2", "1.0~rc1",  1);

    /* epoch */
    check_cmp("1:1.0", "1.0",     1);
    check_cmp("1.0",   "1:1.0",  -1);
    check_cmp("2:1.0", "1:2.0",   1);

    /* release */
    check_cmp("1.0-1",  "1.0-2", -1);
    check_cmp("1.0-2",  "1.0-1",  1);
    check_cmp("1.0-1",  "1.0",    1);

    /* tricky cases */
    check_cmp("1.0a",   "1.0b",  -1);
    check_cmp("1.0b",   "1.0a",   1);
    check_cmp("1.2.3",  "1.2",    1);
    check_cmp("1.2",    "1.2.3", -1);
    check_cmp("1.0.0",  "1.0",    0);   /* trailing .0 should be equal */

    printf("\n  Satisfies tests:\n\n");

    check_sat("2.0", DEP_EQ, "2.0",  true);
    check_sat("2.1", DEP_EQ, "2.0",  false);
    check_sat("2.0", DEP_GE, "1.9",  true);
    check_sat("1.9", DEP_GE, "2.0",  false);
    check_sat("1.9", DEP_LE, "2.0",  true);
    check_sat("2.0", DEP_LE, "1.9",  false);
    check_sat("2.1", DEP_GT, "2.0",  true);
    check_sat("2.0", DEP_GT, "2.0",  false);
    check_sat("1.9", DEP_LT, "2.0",  true);
    check_sat("2.0", DEP_LT, "2.0",  false);
    check_sat("anything", DEP_ANY, NULL, true);

    printf("\n  Validity tests:\n\n");

    check_valid("1.0",         true);
    check_valid("1.0.0",       true);
    check_valid("2:1.0-1",     true);
    check_valid("1.0~rc1",     true);
    check_valid("1.0+git1234", true);
    check_valid("",            false);
    check_valid(NULL,          false);
    check_valid("/bad",        false);
    check_valid("1 0",         false);

    printf("\n  ──────────────────────────────────\n");
    printf("  Results: %d passed, %d failed\n\n", pass, fail);

    return fail ? 1 : 0;
}
