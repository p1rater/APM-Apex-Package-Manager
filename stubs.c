#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* apex_handle_t;
typedef void* apex_pkg_t;

int apex_db_open(apex_handle_t *h) { return 0; }
void apex_db_close(apex_handle_t *h) {}
void* apex_db_verify(void* h, const char* n, size_t* c) { return NULL; }
void* apex_gpg_list_keys() { return NULL; }
int apex_gpg_import_key(const char* f) { return 0; }
int apex_gpg_delete_key(const char* f) { return 0; }
int apex_pkg_remove(void* h, const char* p, void* o) { return 0; }
int apex_run_system_hooks(void* h) { return 0; }
int apex_net_sync(void* h, int f) { return 0; }
void* apex_db_find_provider(void* h, const char* p) { return NULL; }
void* apex_db_find_repo(void* h, const char* n) { return NULL; }
int apex_is_root() { return 1; }
int apex_lock_acquire(void* h) { return 0; }
void apex_lock_release(void* h) {}
long apex_file_size(const char* p) { return 0; }
int apex_term_width() { return 80; }
void apex_dir_walk(const char* p, void* f, void* d) {}
char* apex_human_size(long s) { return (char*)"0B"; }
int apex_rmdir_r(const char* p) { return 0; }
int apex_mkdir_p(const char* p) { return 0; }
void* apex_solver_orphans(void* h) { return NULL; }
void* apex_solver_remove(void* h, const char** n, size_t s, int f) { return NULL; }
void* apex_solver_upgrade(void* h, int f) { return NULL; }
void apex_txn_print(void* t, FILE* f) {}
void apex_solve_result_free(void* r) {}

#ifdef __cplusplus
}
#endif
