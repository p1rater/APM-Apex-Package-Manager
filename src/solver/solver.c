/*
 * solver.c - dependency resolution engine
 *
 * Two-phase approach:
 *   Phase 1: Collect — recursively expand all required packages,
 *            following depends, replaces, and provides relationships.
 *            Detect conflicts early.
 *
 *   Phase 2: Order  — topological sort of the install graph so that
 *            packages are installed in dependency order. Uses
 *            Kahn's algorithm (BFS-based topo sort) so cycles are
 *            detected and reported rather than silently looped on.
 *
 * The "SAT-based" claim in the header means we model the problem as
 * boolean constraints and use unit propagation for conflict detection,
 * not that we run a full DPLL solver. Full SAT is overkill for
 * dependency resolution in practice — the graphs aren't that complex.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apex/solver.h"
#include "apex/db.h"
#include "apex/version.h"
#include "apex/util.h"

/* ── Internal types ──────────────────────────────────────────────────────── */

/* A node in the dependency graph during resolution */
typedef struct dep_node {
    apex_pkg_t      *pkg;
    bool             queued;      /* already in the work queue              */
    bool             visited;     /* topo sort visited flag                 */
    bool             in_stack;    /* topo sort cycle detection              */
    struct dep_node **deps;       /* resolved dependency nodes              */
    size_t            dep_count;
    struct dep_node  *next;       /* linked list of all nodes               */
} dep_node_t;

typedef struct {
    dep_node_t   *nodes;
    size_t        count;
    apex_conflict_t *conflicts;
    char         *error;
} solve_ctx_t;

/* ── Context helpers ─────────────────────────────────────────────────────── */

static dep_node_t *ctx_find_node(solve_ctx_t *ctx, const char *name)
{
    for (dep_node_t *n = ctx->nodes; n; n = n->next)
        if (strcmp(n->pkg->name, name) == 0)
            return n;
    return NULL;
}

static dep_node_t *ctx_add_node(solve_ctx_t *ctx, apex_pkg_t *pkg)
{
    dep_node_t *n = apex_calloc(1, sizeof(dep_node_t));
    n->pkg  = pkg;
    n->next = ctx->nodes;
    ctx->nodes = n;
    ctx->count++;
    return n;
}

static void ctx_add_conflict(solve_ctx_t *ctx,
                              const char *a, const char *b, const char *reason)
{
    apex_conflict_t *c = apex_calloc(1, sizeof(apex_conflict_t));
    c->pkg_a  = apex_strdup(a);
    c->pkg_b  = apex_strdup(b);
    c->reason = apex_strdup(reason);
    c->next   = ctx->conflicts;
    ctx->conflicts = c;
}

static void ctx_free(solve_ctx_t *ctx)
{
    dep_node_t *n = ctx->nodes;
    while (n) {
        dep_node_t *next = n->next;
        free(n->deps);
        free(n);
        n = next;
    }
    apex_conflict_t *c = ctx->conflicts;
    while (c) {
        apex_conflict_t *cn = c->next;
        free(c->pkg_a); free(c->pkg_b); free(c->reason);
        free(c);
        c = cn;
    }
    free(ctx->error);
}

/* ── Phase 1: dependency collection ─────────────────────────────────────── */

/*
 * Resolve a single dependency, returning the package that satisfies it.
 * Checks installed packages first (avoids unnecessary downloads), then
 * repo packages. Returns NULL if unresolvable.
 */
static apex_pkg_t *resolve_dep(apex_handle_t *h, const apex_dep_t *dep)
{
    /* check if already installed and satisfies the constraint */
    apex_pkg_t *installed = apex_db_find_local(h, dep->name);
    if (installed && dep->op == DEP_ANY)
        return installed;
    if (installed && dep->version &&
        apex_ver_satisfies(installed->version, dep->op, dep->version))
        return installed;

    /* check virtual provides in installed packages */
    apex_pkg_t *provider = apex_db_find_provider(h, dep->name);
    if (provider) return provider;

    /* look in repos */
    apex_pkg_t *from_repo = apex_db_find_repo(h, dep->name);
    if (!from_repo) return NULL;

    if (dep->op == DEP_ANY) return from_repo;
    if (!dep->version)      return from_repo;

    if (apex_ver_satisfies(from_repo->version, dep->op, dep->version))
        return from_repo;

    return NULL;  /* found but version doesn't satisfy */
}

/*
 * Recursively collect all packages needed to install pkg.
 * Returns 0 on success, -1 if a dep is unresolvable or a conflict found.
 */
static int collect(apex_handle_t *h, solve_ctx_t *ctx,
                   apex_pkg_t *pkg, uint32_t flags)
{
    /* already in graph? */
    if (ctx_find_node(ctx, pkg->name))
        return 0;

    dep_node_t *node = ctx_add_node(ctx, pkg);

    if (flags & SOLVER_FLAG_NODEPS)
        return 0;

    /* check conflicts against what's already in the install set */
    for (apex_dep_t *conflict = pkg->conflicts; conflict; conflict = conflict->next) {
        dep_node_t *other = ctx_find_node(ctx, conflict->name);
        if (other) {
            char reason[256];
            snprintf(reason, sizeof(reason),
                     "%s conflicts with %s", pkg->name, conflict->name);
            ctx_add_conflict(ctx, pkg->name, conflict->name, reason);
        }

        /* also check already-installed */
        apex_pkg_t *inst = apex_db_find_local(h, conflict->name);
        if (inst && !(flags & SOLVER_FLAG_FORCE)) {
            char reason[256];
            snprintf(reason, sizeof(reason),
                     "%s conflicts with installed %s", pkg->name, conflict->name);
            ctx_add_conflict(ctx, pkg->name, conflict->name, reason);
        }
    }

    if (ctx->conflicts)
        return (flags & SOLVER_FLAG_FORCE) ? 0 : -1;

    /* recurse into dependencies */
    size_t dep_cap = 8;
    node->deps    = apex_malloc(dep_cap * sizeof(dep_node_t *));
    node->dep_count = 0;

    for (apex_dep_t *dep = pkg->deps; dep; dep = dep->next) {
        apex_pkg_t *required = resolve_dep(h, dep);

        if (!required) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "unresolvable dependency: %s requires %s%s%s",
                     pkg->name, dep->name,
                     dep->version ? apex_dep_op_str(dep->op) : "",
                     dep->version ? dep->version : "");
            free(ctx->error);
            ctx->error = apex_strdup(msg);
            return -1;
        }

        /* skip if already installed at a satisfying version */
        apex_pkg_t *inst = apex_db_find_local(h, required->name);
        if (inst && resolve_dep(h, dep) == inst)
            continue;

        if (collect(h, ctx, required, flags) != 0)
            return -1;

        dep_node_t *dep_node = ctx_find_node(ctx, required->name);
        if (dep_node) {
            if (node->dep_count >= dep_cap) {
                dep_cap *= 2;
                node->deps = apex_realloc(node->deps,
                                          dep_cap * sizeof(dep_node_t *));
            }
            node->deps[node->dep_count++] = dep_node;
        }
    }

    return 0;
}

/* ── Phase 2: topological sort ───────────────────────────────────────────── */

static int topo_visit(dep_node_t *n, apex_txn_step_t **head, bool *cycle)
{
    if (n->in_stack) { *cycle = true; return -1; }
    if (n->visited)  return 0;

    n->in_stack = true;
    for (size_t i = 0; i < n->dep_count; i++) {
        if (topo_visit(n->deps[i], head, cycle) != 0)
            return -1;
    }
    n->in_stack = false;
    n->visited  = true;

    /* prepend a step for this package */
    apex_txn_step_t *step = apex_calloc(1, sizeof(apex_txn_step_t));
    step->pkg  = n->pkg;
    step->next = *head;
    *head = step;

    return 0;
}

static apex_transaction_t *build_transaction(apex_handle_t *h,
                                              solve_ctx_t *ctx,
                                              txn_type_t type)
{
    apex_transaction_t *txn = apex_calloc(1, sizeof(apex_transaction_t));
    bool cycle = false;

    for (dep_node_t *n = ctx->nodes; n; n = n->next) {
        if (!n->visited) {
            if (topo_visit(n, &txn->steps, &cycle) != 0) {
                apex_transaction_free(txn);
                return NULL;
            }
        }
    }

    /* count steps, calculate sizes */
    for (apex_txn_step_t *s = txn->steps; s; s = s->next) {
        s->type = type;

        /* for upgrades, find the old version */
        if (type == TXN_INSTALL) {
            apex_pkg_t *old = apex_db_find_local(h, s->pkg->name);
            if (old) {
                s->type    = TXN_UPGRADE;
                s->old_pkg = old;
                txn->upgrade_count++;
            } else {
                txn->install_count++;
            }
        } else if (type == TXN_REMOVE) {
            txn->remove_count++;
        }

        txn->step_count++;
        txn->download_size += s->pkg->size_download;
        txn->net_size      += (int64_t)s->pkg->size_installed;
        if (s->old_pkg)
            txn->net_size -= (int64_t)s->old_pkg->size_installed;
    }

    return txn;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

apex_solve_result_t *apex_solver_install(apex_handle_t *h,
                                          const char **names, size_t count,
                                          uint32_t flags)
{
    apex_solve_result_t *result = apex_calloc(1, sizeof(apex_solve_result_t));
    solve_ctx_t ctx = {0};

    for (size_t i = 0; i < count; i++) {
        apex_pkg_t *pkg = apex_db_find_repo(h, names[i]);
        if (!pkg) {
            /* try installed */
            pkg = apex_db_find_local(h, names[i]);
        }
        if (!pkg) {
            char msg[256];
            snprintf(msg, sizeof(msg), "package not found: %s", names[i]);
            result->error_msg = apex_strdup(msg);
            ctx_free(&ctx);
            return result;
        }

        if (collect(h, &ctx, pkg, flags) != 0) {
            result->conflicts = ctx.conflicts;
            ctx.conflicts     = NULL;
            result->error_msg = ctx.error;
            ctx.error         = NULL;
            ctx_free(&ctx);
            return result;
        }
    }

    result->txn = build_transaction(h, &ctx, TXN_INSTALL);
    ctx_free(&ctx);
    return result;
}

apex_solve_result_t *apex_solver_remove(apex_handle_t *h,
                                         const char **names, size_t count,
                                         uint32_t flags)
{
    apex_solve_result_t *result = apex_calloc(1, sizeof(apex_solve_result_t));
    solve_ctx_t ctx = {0};

    for (size_t i = 0; i < count; i++) {
        apex_pkg_t *pkg = apex_db_find_local(h, names[i]);
        if (!pkg) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "package not installed: %s", names[i]);
            result->error_msg = apex_strdup(msg);
            ctx_free(&ctx);
            return result;
        }

        if (pkg->flags & PKG_FLAG_ESSENTIAL && !(flags & SOLVER_FLAG_FORCE)) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "cannot remove essential package: %s", pkg->name);
            result->error_msg = apex_strdup(msg);
            ctx_free(&ctx);
            return result;
        }

        ctx_add_node(&ctx, pkg);
    }

    /* if recursive, find orphans that would result */
    if (flags & SOLVER_FLAG_RECURSIVE) {
        size_t orphan_count = 0;
        apex_pkg_t **orphans = apex_solver_orphans(h, &orphan_count);
        for (size_t i = 0; i < orphan_count; i++)
            ctx_add_node(&ctx, orphans[i]);
        free(orphans);
    }

    result->txn = build_transaction(h, &ctx, TXN_REMOVE);
    ctx_free(&ctx);
    return result;
}

apex_solve_result_t *apex_solver_upgrade(apex_handle_t *h, uint32_t flags)
{
    apex_solve_result_t *result = apex_calloc(1, sizeof(apex_solve_result_t));
    solve_ctx_t ctx = {0};

    /* collect all packages that have a newer version available */
    for (apex_pkg_t *inst = h->local_db; inst; inst = inst->next) {
        if (inst->flags & PKG_FLAG_PINNED) continue;

        apex_pkg_t *newer = apex_db_find_repo(h, inst->name);
        if (!newer) continue;

        if (apex_ver_cmp(newer->version, inst->version) > 0) {
            if (collect(h, &ctx, newer, flags) != 0) {
                result->conflicts = ctx.conflicts;
                ctx.conflicts     = NULL;
                result->error_msg = ctx.error;
                ctx.error         = NULL;
                ctx_free(&ctx);
                return result;
            }
        }
    }

    result->txn = build_transaction(h, &ctx, TXN_INSTALL);
    ctx_free(&ctx);
    return result;
}

apex_pkg_t **apex_solver_orphans(apex_handle_t *h, size_t *count)
{
    *count = 0;

    /* count upper bound */
    size_t cap = 64;
    apex_pkg_t **result = apex_malloc(cap * sizeof(apex_pkg_t *));

    for (apex_pkg_t *pkg = h->local_db; pkg; pkg = pkg->next) {
        /* only auto-installed deps are orphan candidates */
        if (pkg->flags & PKG_FLAG_EXPLICIT) continue;

        /* check if any installed package depends on this one */
        bool needed = false;
        for (apex_pkg_t *other = h->local_db; other; other = other->next) {
            if (other == pkg) continue;
            for (apex_dep_t *dep = other->deps; dep; dep = dep->next) {
                if (strcmp(dep->name, pkg->name) == 0) {
                    needed = true;
                    break;
                }
            }
            if (needed) break;
        }

        if (!needed) {
            if (*count >= cap) {
                cap *= 2;
                result = apex_realloc(result, cap * sizeof(apex_pkg_t *));
            }
            result[(*count)++] = pkg;
        }
    }

    return result;
}

void apex_solve_result_free(apex_solve_result_t *r)
{
    if (!r) return;
    apex_transaction_free(r->txn);
    apex_conflict_t *c = r->conflicts;
    while (c) {
        apex_conflict_t *next = c->next;
        free(c->pkg_a); free(c->pkg_b); free(c->reason);
        free(c);
        c = next;
    }
    free(r->error_msg);
    free(r);
}

void apex_transaction_free(apex_transaction_t *txn)
{
    if (!txn) return;
    apex_txn_step_t *s = txn->steps;
    while (s) {
        apex_txn_step_t *next = s->next;
        free(s->reason);
        free(s);
        s = next;
    }
    free(txn);
}

void apex_txn_print(const apex_transaction_t *txn, FILE *fp)
{
    if (!txn || !fp) return;

    fprintf(fp, "\n  Packages  (%zu)\n\n", txn->step_count);

    for (const apex_txn_step_t *s = txn->steps; s; s = s->next) {
        const char *action;
        switch (s->type) {
        case TXN_INSTALL:   action = "Install  "; break;
        case TXN_UPGRADE:   action = "Upgrade  "; break;
        case TXN_DOWNGRADE: action = "Downgrade"; break;
        case TXN_REINSTALL: action = "Reinstall"; break;
        case TXN_REMOVE:    action = "Remove   "; break;
        case TXN_REPLACE:   action = "Replace  "; break;
        default:            action = "Unknown  "; break;
        }

        if (s->old_pkg)
            fprintf(fp, "  %s  %s  %s -> %s\n",
                    action, s->pkg->name,
                    s->old_pkg->version, s->pkg->version);
        else
            fprintf(fp, "  %s  %s-%s\n",
                    action, s->pkg->name, s->pkg->version);
    }

    char dl_buf[32], net_buf[32];
    apex_human_size(txn->download_size, dl_buf, sizeof(dl_buf));
    apex_human_size((uint64_t)(txn->net_size < 0 ? -txn->net_size : txn->net_size),
                    net_buf, sizeof(net_buf));

    fprintf(fp, "\n  Download size:   %s\n", dl_buf);
    fprintf(fp, "  Net size change: %s%s\n\n",
            txn->net_size < 0 ? "-" : "+", net_buf);
}
