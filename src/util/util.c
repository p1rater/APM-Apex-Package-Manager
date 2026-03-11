/*
 * util.c - utility function implementations
 *
 * Nothing interesting architecturally — just the implementations
 * of all the helpers declared in util.h. Kept in one file so the
 * compiler can inline aggressively across the whole unit.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
#include <pwd.h>

#include "apex/util.h"

/* ── Memory ──────────────────────────────────────────────────────────────── */

void *apex_malloc(size_t n)
{
    void *p = malloc(n);
    if (!p && n) {
        fprintf(stderr, "apex: out of memory (%zu bytes)\n", n);
        abort();
    }
    return p;
}

void *apex_calloc(size_t nmemb, size_t size)
{
    void *p = calloc(nmemb, size);
    if (!p && nmemb && size) {
        fprintf(stderr, "apex: out of memory (%zu * %zu bytes)\n", nmemb, size);
        abort();
    }
    return p;
}

void *apex_realloc(void *ptr, size_t n)
{
    void *p = realloc(ptr, n);
    if (!p && n) {
        fprintf(stderr, "apex: out of memory (realloc %zu bytes)\n", n);
        abort();
    }
    return p;
}

char *apex_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = apex_malloc(len);
    memcpy(d, s, len);
    return d;
}

char *apex_strndup(const char *s, size_t n)
{
    if (!s) return NULL;
    size_t len = strnlen(s, n);
    char *d = apex_malloc(len + 1);
    memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

void apex_free(void *ptr)
{
    free(ptr);
}

void apex_zfree(void *ptr, size_t len)
{
    if (ptr) {
        memset(ptr, 0, len);
        free(ptr);
    }
}

/* ── String helpers ──────────────────────────────────────────────────────── */

char *apex_strtrim(char *s)
{
    if (!s) return s;

    /* trim leading */
    while (*s && isspace((unsigned char)*s))
        s++;

    if (!*s) return s;

    /* trim trailing */
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        end--;
    *(end + 1) = '\0';

    return s;
}

int apex_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int diff = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (diff) return diff;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

bool apex_startswith(const char *s, const char *prefix)
{
    if (!s || !prefix) return false;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool apex_endswith(const char *s, const char *suffix)
{
    if (!s || !suffix) return false;
    size_t sl = strlen(s), pl = strlen(suffix);
    if (pl > sl) return false;
    return strcmp(s + sl - pl, suffix) == 0;
}

char **apex_strsplit(const char *s, char delim, size_t *count)
{
    if (!s) { *count = 0; return NULL; }

    /* count fields */
    size_t n = 1;
    for (const char *p = s; *p; p++)
        if (*p == delim) n++;

    char **arr = apex_malloc((n + 1) * sizeof(char *));
    size_t i = 0;
    const char *start = s;

    for (const char *p = s; ; p++) {
        if (*p == delim || *p == '\0') {
            arr[i++] = apex_strndup(start, (size_t)(p - start));
            start = p + 1;
            if (!*p) break;
        }
    }
    arr[i] = NULL;
    if (count) *count = n;
    return arr;
}

void apex_strarray_free(char **arr)
{
    if (!arr) return;
    for (char **p = arr; *p; p++)
        free(*p);
    free(arr);
}

int apex_snprintf(char *buf, size_t sz, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sz, fmt, ap);
    va_end(ap);
    if (sz > 0) buf[sz - 1] = '\0';
    return n;
}

char *apex_path_join(const char *base, const char *part)
{
    if (!base || !*base) return apex_strdup(part);
    if (!part  || !*part) return apex_strdup(base);

    size_t blen = strlen(base);
    size_t plen = strlen(part);
    bool slash  = base[blen - 1] == '/';

    char *out = apex_malloc(blen + plen + 2);
    memcpy(out, base, blen);
    if (!slash) out[blen++] = '/';
    memcpy(out + blen, part, plen + 1);
    return out;
}

const char *apex_basename(const char *path)
{
    if (!path || !*path) return ".";
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

char *apex_dirname(const char *path)
{
    if (!path || !*path) return apex_strdup(".");
    const char *p = strrchr(path, '/');
    if (!p) return apex_strdup(".");
    if (p == path) return apex_strdup("/");
    return apex_strndup(path, (size_t)(p - path));
}

/* ── File / directory helpers ────────────────────────────────────────────── */

bool apex_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool apex_dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int apex_mkdir_p(const char *path, mode_t mode)
{
    char *tmp = apex_strdup(path);
    int rc = 0;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) { rc = -1; break; }
            *p = '/';
        }
    }
    if (rc == 0)
        if (mkdir(tmp, mode) != 0 && errno != EEXIST)
            rc = -1;

    free(tmp);
    return rc;
}

int apex_rmdir_r(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return -1;

    struct dirent *ent;
    int rc = 0;

    while ((ent = readdir(d))) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;

        char *child = apex_path_join(path, ent->d_name);
        struct stat st;

        if (lstat(child, &st) == 0) {
            if (S_ISDIR(st.st_mode))
                rc = apex_rmdir_r(child);
            else
                rc = unlink(child);
        }
        free(child);
        if (rc) break;
    }
    closedir(d);
    if (!rc) rmdir(path);
    return rc;
}

int apex_copy_file(const char *src, const char *dst)
{
    int fdin = open(src, O_RDONLY);
    if (fdin < 0) return -1;

    struct stat st;
    fstat(fdin, &st);

    int fdout = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
    if (fdout < 0) { close(fdin); return -1; }

    char buf[65536];
    ssize_t n;
    int rc = 0;

    while ((n = read(fdin, buf, sizeof(buf))) > 0) {
        if (write(fdout, buf, (size_t)n) != n) { rc = -1; break; }
    }
    if (n < 0) rc = -1;

    close(fdin);
    fsync(fdout);
    close(fdout);
    return rc;
}

int apex_move_file(const char *src, const char *dst)
{
    if (rename(src, dst) == 0) return 0;
    /* cross-device: copy + unlink */
    if (apex_copy_file(src, dst) == 0) {
        unlink(src);
        return 0;
    }
    return -1;
}

long apex_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

char *apex_read_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);

    if (sz < 0) { fclose(fp); return NULL; }

    char *buf = apex_malloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);

    buf[rd] = '\0';
    if (out_len) *out_len = rd;
    return buf;
}

int apex_write_file(const char *path, const void *data, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t n = write(fd, data, len);
    fsync(fd);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}

int apex_atomic_write(const char *path, const void *data, size_t len)
{
    char *tmp = NULL;
    int rc;

    /* write to .tmp then rename — atomic on same filesystem */
    if (asprintf(&tmp, "%s.tmp.%d", path, getpid()) < 0) return -1;

    rc = apex_write_file(tmp, data, len);
    if (rc == 0)
        rc = rename(tmp, path);

    free(tmp);
    return rc;
}

int apex_dir_walk(const char *dir, apex_dir_cb cb, void *udata)
{
    DIR *d = opendir(dir);
    if (!d) return -1;

    struct dirent *ent;
    int rc = 0;

    while ((ent = readdir(d))) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;

        char *child = apex_path_join(dir, ent->d_name);
        struct stat st;
        bool is_dir = false;

        if (lstat(child, &st) == 0)
            is_dir = S_ISDIR(st.st_mode);

        rc = cb(child, is_dir, udata);
        free(child);
        if (rc) break;
    }
    closedir(d);
    return rc;
}

/* ── Hex / binary ─────────────────────────────────────────────────────────── */

static const char _hextab[] = "0123456789abcdef";

char *apex_bin2hex(const uint8_t *bin, size_t len)
{
    char *hex = apex_malloc(len * 2 + 1);
    for (size_t i = 0; i < len; i++) {
        hex[i * 2]     = _hextab[bin[i] >> 4];
        hex[i * 2 + 1] = _hextab[bin[i] & 0xf];
    }
    hex[len * 2] = '\0';
    return hex;
}

int apex_hex2bin(const char *hex, uint8_t *out, size_t out_max)
{
    size_t len = strlen(hex);
    if (len % 2 != 0) return -1;

    size_t bytes = len / 2;
    if (bytes > out_max) return -1;

    for (size_t i = 0; i < bytes; i++) {
        char hi = hex[i * 2], lo = hex[i * 2 + 1];
        int h = isdigit((unsigned char)hi) ? hi - '0' :
                tolower((unsigned char)hi) - 'a' + 10;
        int l = isdigit((unsigned char)lo) ? lo - '0' :
                tolower((unsigned char)lo) - 'a' + 10;
        if (h < 0 || h > 15 || l < 0 || l > 15) return -1;
        out[i] = (uint8_t)((h << 4) | l);
    }
    return (int)bytes;
}

/* ── Human-readable sizes ────────────────────────────────────────────────── */

void apex_human_size(uint64_t bytes, char *buf, size_t bufsz)
{
    static const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double val = (double)bytes;
    int u = 0;

    while (val >= 1024.0 && u < 4) { val /= 1024.0; u++; }

    if (u == 0)
        snprintf(buf, bufsz, "%llu %s", (unsigned long long)bytes, units[u]);
    else
        snprintf(buf, bufsz, "%.2f %s", val, units[u]);
}

/* ── Time ─────────────────────────────────────────────────────────────────── */

void apex_timestamp(char *buf, size_t bufsz)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, bufsz, "%Y-%m-%dT%H:%M:%S", &tm);
}

uint64_t apex_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ── Locking ──────────────────────────────────────────────────────────────── */

int apex_lock_acquire(const char *path)
{
    int fd = open(path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return -1;

    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return -1;
    }

    /* write PID so users can see who holds the lock */
    char pidbuf[32];
    snprintf(pidbuf, sizeof(pidbuf), "%d\n", getpid());
    ftruncate(fd, 0);
    write(fd, pidbuf, strlen(pidbuf));

    return fd;
}

void apex_lock_release(int fd, const char *path)
{
    if (fd >= 0) {
        flock(fd, LOCK_UN);
        close(fd);
    }
    if (path) unlink(path);
}

/* ── Misc ─────────────────────────────────────────────────────────────────── */

int apex_run(const char *cmd, char **out, size_t *out_len)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char *buf = NULL;
    size_t len = 0, cap = 0;
    char tmp[4096];
    size_t n;

    while ((n = fread(tmp, 1, sizeof(tmp), fp)) > 0) {
        if (len + n + 1 > cap) {
            cap = (len + n + 1) * 2;
            buf = apex_realloc(buf, cap);
        }
        memcpy(buf + len, tmp, n);
        len += n;
    }
    if (buf) buf[len] = '\0';

    int status = pclose(fp);

    if (out) *out = buf;
    else free(buf);

    if (out_len) *out_len = len;
    return WEXITSTATUS(status);
}

char *apex_shell_escape(const char *s)
{
    if (!s) return apex_strdup("''");
    size_t n = 2; /* surrounding quotes */
    for (const char *p = s; *p; p++)
        n += (*p == '\'') ? 4 : 1;

    char *out = apex_malloc(n + 1);
    char *w = out;
    *w++ = '\'';
    for (const char *p = s; *p; p++) {
        if (*p == '\'') {
            *w++ = '\''; *w++ = '\\'; *w++ = '\''; *w++ = '\'';
        } else {
            *w++ = *p;
        }
    }
    *w++ = '\'';
    *w   = '\0';
    return out;
}

bool apex_is_root(void)
{
    return geteuid() == 0;
}

int apex_term_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}
