/*
 * Host tests for `process_destroy` (M4 phases 6 + 6.5 + 6.6).
 *
 * Phase 6 locks the cleanup contract:
 *   - NULL is a no-op.
 *   - PROC_STATE_UNUSED is a no-op (idempotent).
 *   - PROC_STATE_EMBRYO -> address_space released, fds cleared,
 *     state == PROC_STATE_UNUSED.
 *   - PROC_STATE_ZOMBIE -> same teardown as embryo.
 *   - process_count() reflects the freed slot.
 *   - The freed slot is reusable: a follow-up process_create returns
 *     a slot pointer (possibly the same one) and the new process
 *     has a fresh pid.
 *
 * Phase 6.5 adds the process-tree linkage contract:
 *   - process_fork links the child into the parent's `children`
 *     list (head insertion, LIFO).
 *   - process_destroy walks `children`, orphans each one
 *     (parent=NULL, ppid=0, next_sibling=NULL), then unlinks
 *     itself from its parent's `children` list.
 *   - Orphaned children can later be destroyed without dereferencing
 *     a stale parent pointer.
 *   - Multi-child trees orphan all children on parent destroy.
 *
 * Phase 6.6 adds the zombie-reaping contract:
 *   - process_kill records exit_code = 128 + (signal & 0x7F) on the
 *     killed slot for parent wait() consumers.
 *   - process_kill on a parented process leaves it ZOMBIE for the
 *     parent's wait() to reap.
 *   - process_kill on an orphan auto-reaps inline (slot returns to
 *     UNUSED before process_kill returns).
 *   - process_reap_orphans() walks the table, destroys orphan
 *     zombies, returns the count, and leaves alive/parented slots
 *     untouched. Idempotent across repeated calls.
 *
 * Together these tests lock the only writer surface for the
 * `parent`/`children`/`next_sibling` triple in include/kernel/process.h
 * and the public reaping API.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/process.h"

static int tests_run = 0;
static int tests_passed = 0;
#define TEST(name) do { tests_run++; printf("  %-58s ", name); } while (0)
#define PASS()     do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg)  printf("FAIL: %s\n", msg)

static void reset_world(void) {
    process_system_init();
}

static void test_null_is_noop(void) {
    reset_world();
    process_destroy(NULL);

    TEST("process_destroy(NULL) is a no-op");
    if (process_count() == 0) PASS();
    else FAIL("count drift after NULL destroy");
}

static void test_unused_is_noop(void) {
    reset_world();
    /* Borrow a slot via process_at_index without flipping its state. */
    struct process *p = process_at_index(0);
    if (!p) { TEST("process_at_index sanity"); FAIL("NULL slot"); return; }

    process_destroy(p);

    TEST("process_destroy on UNUSED slot is a no-op");
    if (process_count() == 0) PASS();
    else FAIL("count drift after UNUSED destroy");
}

static void test_destroy_embryo(void) {
    reset_world();
    struct process *p = process_create("embryo", 1000, 1000);

    TEST("setup: process_create returns a slot");
    if (p) PASS(); else { FAIL("process_create returned NULL"); return; }

    TEST("setup: address_space allocated");
    if (p->address_space != NULL) PASS();
    else FAIL("address_space NULL after create");

    TEST("setup: process_count == 1");
    if (process_count() == 1) PASS();
    else FAIL("count != 1");

    process_destroy(p);

    TEST("destroy: process_count back to 0");
    if (process_count() == 0) PASS();
    else FAIL("count != 0 after destroy");

    TEST("destroy: state == PROC_STATE_UNUSED");
    if (p->state == PROC_STATE_UNUSED) PASS();
    else FAIL("state not UNUSED");

    TEST("destroy: address_space cleared");
    if (p->address_space == NULL) PASS();
    else FAIL("address_space leaked");

    TEST("destroy: fds[0..PROCESS_FD_MAX-1] cleared");
    int dirty = 0;
    for (int i = 0; i < PROCESS_FD_MAX; i++) {
        if (p->fds[i].type != 0 || p->fds[i].private_data != NULL) {
            dirty = 1; break;
        }
    }
    if (!dirty) PASS();
    else FAIL("fd slot left dirty after destroy");
}

static void test_destroy_zombie(void) {
    reset_world();
    struct process *p = process_create("zombie", 0, 0);
    if (!p) { TEST("setup zombie"); FAIL("create failed"); return; }
    p->state = PROC_STATE_ZOMBIE;
    p->exit_code = 42;

    process_destroy(p);

    TEST("zombie destroy -> state UNUSED");
    if (p->state == PROC_STATE_UNUSED) PASS();
    else FAIL("zombie not torn down");

    TEST("zombie destroy -> address_space cleared");
    if (p->address_space == NULL) PASS();
    else FAIL("zombie AS leaked");
}

static void test_idempotent(void) {
    reset_world();
    struct process *p = process_create("dup", 0, 0);
    if (!p) { TEST("setup"); FAIL("create failed"); return; }

    process_destroy(p);
    process_destroy(p);   /* second call should be a no-op */

    TEST("double destroy stays UNUSED");
    if (p->state == PROC_STATE_UNUSED) PASS();
    else FAIL("state drift after double destroy");

    TEST("double destroy keeps count == 0");
    if (process_count() == 0) PASS();
    else FAIL("count drift");
}

static void test_slot_reusable(void) {
    reset_world();
    struct process *first = process_create("first", 0, 0);
    if (!first) { TEST("setup"); FAIL("create failed"); return; }
    uint32_t first_pid = first->pid;
    process_destroy(first);

    struct process *second = process_create("second", 0, 0);

    TEST("after destroy, process_create succeeds again");
    if (second) PASS();
    else { FAIL("create after destroy failed"); return; }

    TEST("reused slot has a fresh, larger pid");
    if (second->pid > first_pid) PASS();
    else FAIL("pid did not advance");

    TEST("reused slot has the new name");
    if (strcmp(second->name, "second") == 0) PASS();
    else FAIL("name not refreshed");

    TEST("reused slot has fresh, non-NULL address_space");
    if (second->address_space != NULL) PASS();
    else FAIL("AS not re-allocated");
}

/* ---- Phase 6.5: process-tree linkage --------------------------- */

/* Count how many siblings are reachable from `head` via next_sibling. */
static size_t count_chain(const struct process *head) {
    size_t n = 0;
    while (head) {
        n++;
        head = head->next_sibling;
    }
    return n;
}

/* Returns 1 if `needle` is reachable from `head` via next_sibling,
 * 0 otherwise. */
static int chain_contains(const struct process *head,
                          const struct process *needle) {
    while (head) {
        if (head == needle) return 1;
        head = head->next_sibling;
    }
    return 0;
}

static void test_fork_links_child(void) {
    reset_world();
    struct process *parent = process_create("parent", 0, 0);
    if (!parent) { TEST("setup parent"); FAIL("create"); return; }
    struct process *child = process_fork(parent);
    if (!child) { TEST("setup fork"); FAIL("fork"); return; }

    TEST("fork: child->parent == parent");
    if (child->parent == parent) PASS();
    else FAIL("child->parent mismatch");

    TEST("fork: child->ppid == parent->pid");
    if (child->ppid == parent->pid) PASS();
    else FAIL("child->ppid mismatch");

    TEST("fork: parent->children is the new child (LIFO head)");
    if (parent->children == child) PASS();
    else FAIL("parent->children does not point at child");

    TEST("fork: child->next_sibling == NULL (only child)");
    if (child->next_sibling == NULL) PASS();
    else FAIL("singleton child has stale next_sibling");

    TEST("fork: parent has exactly one child in its list");
    if (count_chain(parent->children) == 1) PASS();
    else FAIL("children count mismatch");
}

static void test_fork_lifo_ordering(void) {
    reset_world();
    struct process *parent = process_create("parent", 0, 0);
    if (!parent) { TEST("setup parent"); FAIL("create"); return; }
    struct process *a = process_fork(parent);
    struct process *b = process_fork(parent);
    struct process *c = process_fork(parent);
    if (!a || !b || !c) { TEST("setup forks"); FAIL("fork"); return; }

    TEST("fork: parent->children chain has all three");
    if (count_chain(parent->children) == 3) PASS();
    else FAIL("count != 3");

    TEST("fork: chain reaches each child exactly once");
    int has_a = chain_contains(parent->children, a);
    int has_b = chain_contains(parent->children, b);
    int has_c = chain_contains(parent->children, c);
    if (has_a && has_b && has_c) PASS();
    else FAIL("missing child in chain");

    /* LIFO insertion: c was forked last so it must be at the head.
     * The README of process.c documents that the order is NOT
     * contractual, so this assertion is only a smoke for the current
     * implementation. If the policy ever changes (e.g. tail insertion
     * for fairness), update both this test and the docs together. */
    TEST("fork: most recent child is at head (LIFO, current policy)");
    if (parent->children == c) PASS();
    else FAIL("head is not the latest fork (policy regression)");
}

static void test_destroy_unlinks_from_parent(void) {
    reset_world();
    struct process *parent = process_create("parent", 0, 0);
    if (!parent) { TEST("setup parent"); FAIL("create"); return; }
    struct process *a = process_fork(parent);
    struct process *b = process_fork(parent);
    if (!a || !b) { TEST("setup forks"); FAIL("fork"); return; }

    /* Destroy `a` while `b` is still alive. Parent's chain should
     * shrink to just `b`; `a`'s slot should be UNUSED. */
    process_destroy(a);

    TEST("destroy child: parent's chain has exactly one survivor");
    if (count_chain(parent->children) == 1) PASS();
    else FAIL("count != 1 after one destroy");

    TEST("destroy child: survivor is `b`");
    if (parent->children == b) PASS();
    else FAIL("wrong survivor at head");

    TEST("destroy child: parent's chain no longer contains `a`");
    if (!chain_contains(parent->children, a)) PASS();
    else FAIL("destroyed child still in chain");

    /* Destroy `b` too. Chain should empty out without touching the
     * parent's slot. */
    process_destroy(b);

    TEST("destroy last child: parent->children is NULL");
    if (parent->children == NULL) PASS();
    else FAIL("parent->children leaked");

    TEST("destroy last child: parent itself is still EMBRYO");
    if (parent->state == PROC_STATE_EMBRYO) PASS();
    else FAIL("parent state drifted");
}

static void test_destroy_orphans_children(void) {
    reset_world();
    struct process *parent = process_create("parent", 0, 0);
    if (!parent) { TEST("setup parent"); FAIL("create"); return; }
    struct process *a = process_fork(parent);
    struct process *b = process_fork(parent);
    struct process *c = process_fork(parent);
    if (!a || !b || !c) { TEST("setup forks"); FAIL("fork"); return; }

    process_destroy(parent);

    TEST("destroy parent: child a is orphaned (parent=NULL)");
    if (a->parent == NULL) PASS();
    else FAIL("a->parent leaked");

    TEST("destroy parent: child b is orphaned (parent=NULL)");
    if (b->parent == NULL) PASS();
    else FAIL("b->parent leaked");

    TEST("destroy parent: child c is orphaned (parent=NULL)");
    if (c->parent == NULL) PASS();
    else FAIL("c->parent leaked");

    TEST("destroy parent: orphan ppid resets to 0");
    if (a->ppid == 0 && b->ppid == 0 && c->ppid == 0) PASS();
    else FAIL("ppid leaked across destroy");

    TEST("destroy parent: orphan next_sibling resets to NULL");
    if (a->next_sibling == NULL &&
        b->next_sibling == NULL &&
        c->next_sibling == NULL) PASS();
    else FAIL("next_sibling leaked across destroy");

    /* Orphans keep running: state must remain whatever it was
     * (EMBRYO from process_create), NOT flipped to UNUSED. */
    TEST("destroy parent: orphans stay alive (state preserved)");
    if (a->state == PROC_STATE_EMBRYO &&
        b->state == PROC_STATE_EMBRYO &&
        c->state == PROC_STATE_EMBRYO) PASS();
    else FAIL("orphan state changed unexpectedly");
}

static void test_orphan_destroy_safe(void) {
    /* After parent destroy, the orphan must still be destroyable
     * without dereferencing the (now-stale) parent slot. */
    reset_world();
    struct process *parent = process_create("parent", 0, 0);
    if (!parent) { TEST("setup parent"); FAIL("create"); return; }
    struct process *child = process_fork(parent);
    if (!child) { TEST("setup fork"); FAIL("fork"); return; }

    process_destroy(parent);

    TEST("orphan destroy: parent already torn down (state=UNUSED)");
    if (parent->state == PROC_STATE_UNUSED) PASS();
    else FAIL("parent not marked UNUSED");

    /* The orphan's parent pointer must already be NULL by the time
     * we get here, otherwise process_destroy would walk into the
     * stale parent slot below. */
    TEST("orphan destroy: orphan->parent is NULL pre-destroy");
    if (child->parent == NULL) PASS();
    else FAIL("orphan still references parent slot");

    process_destroy(child);

    TEST("orphan destroy: orphan now UNUSED");
    if (child->state == PROC_STATE_UNUSED) PASS();
    else FAIL("orphan not torn down");

    TEST("orphan destroy: process_count back to 0");
    if (process_count() == 0) PASS();
    else FAIL("count drift after orphan destroy");
}

/* ---- Phase 6.6: zombie reaping --------------------------------- */

static void test_kill_orphan_auto_reaps(void) {
    reset_world();
    struct process *p = process_create("orphan", 0, 0);
    if (!p) { TEST("setup orphan"); FAIL("create"); return; }

    TEST("setup: orphan has parent==NULL");
    if (p->parent == NULL) PASS();
    else FAIL("expected orphan; current_proc unset?");

    uint32_t pid = p->pid;
    int rc = process_kill(pid, 9);

    TEST("process_kill returns 0 on existing orphan");
    if (rc == 0) PASS();
    else FAIL("kill returned non-zero");

    /* Auto-reap: the slot should be UNUSED when kill() returns,
     * NOT ZOMBIE. The slot pointer itself stays valid (proc_table
     * is a static array) but its state is reset. */
    TEST("kill on orphan auto-reaps -> state UNUSED");
    if (p->state == PROC_STATE_UNUSED) PASS();
    else FAIL("orphan still ZOMBIE after kill");

    TEST("kill on orphan -> process_count back to 0");
    if (process_count() == 0) PASS();
    else FAIL("count drift after orphan kill");
}

static void test_kill_parented_stays_zombie(void) {
    reset_world();
    struct process *parent = process_create("parent", 0, 0);
    if (!parent) { TEST("setup parent"); FAIL("create"); return; }
    struct process *child = process_fork(parent);
    if (!child) { TEST("setup fork"); FAIL("fork"); return; }

    int rc = process_kill(child->pid, 11);

    TEST("process_kill returns 0 on parented child");
    if (rc == 0) PASS();
    else FAIL("kill returned non-zero");

    /* Parented children stay ZOMBIE so the parent's eventual
     * wait() can read exit_code. */
    TEST("kill on parented -> state ZOMBIE (not auto-reaped)");
    if (child->state == PROC_STATE_ZOMBIE) PASS();
    else FAIL("parented child unexpectedly reaped");

    TEST("kill on parented -> exit_code == 128 + signal");
    if (child->exit_code == 128 + 11) PASS();
    else FAIL("exit_code mismatch");

    TEST("kill on parented -> still in parent's children list");
    if (parent->children == child) PASS();
    else FAIL("zombie child should still be linked until reap");
}

static void test_kill_signal_clamping(void) {
    /* Signals above 127 must be masked to 7 bits (POSIX-ish
     * WTERMSIG semantics). The mask is `signal & 0x7F`. */
    reset_world();
    struct process *parent = process_create("parent", 0, 0);
    if (!parent) { TEST("setup parent"); FAIL("create"); return; }
    struct process *child = process_fork(parent);
    if (!child) { TEST("setup fork"); FAIL("fork"); return; }

    process_kill(child->pid, 0xFF); /* signal 255 */

    TEST("kill clamps signal to 7 bits in exit_code");
    if (child->exit_code == 128 + (0xFF & 0x7F)) PASS();
    else FAIL("clamp missing; exit_code overflowed");
}

static void test_kill_unknown_pid(void) {
    reset_world();
    int rc = process_kill(99999, 9);
    TEST("process_kill on unknown pid returns -1");
    if (rc == -1) PASS();
    else FAIL("expected -1 for missing pid");
}

static void test_reap_orphans_empty(void) {
    reset_world();
    size_t reaped = process_reap_orphans();
    TEST("process_reap_orphans on empty table returns 0");
    if (reaped == 0) PASS();
    else FAIL("count != 0 with no zombies");
}

static void test_reap_orphans_skips_alive(void) {
    reset_world();
    struct process *a = process_create("alive_a", 0, 0);
    struct process *b = process_create("alive_b", 0, 0);
    if (!a || !b) { TEST("setup"); FAIL("create"); return; }

    size_t reaped = process_reap_orphans();

    TEST("reap_orphans skips EMBRYO orphans");
    if (reaped == 0) PASS();
    else FAIL("reaped a non-zombie");

    TEST("alive orphans still reachable post-sweep");
    if (a->state == PROC_STATE_EMBRYO &&
        b->state == PROC_STATE_EMBRYO) PASS();
    else FAIL("EMBRYO state changed unexpectedly");
}

static void test_reap_orphans_skips_parented_zombies(void) {
    reset_world();
    struct process *parent = process_create("parent", 0, 0);
    if (!parent) { TEST("setup parent"); FAIL("create"); return; }
    struct process *child = process_fork(parent);
    if (!child) { TEST("setup fork"); FAIL("fork"); return; }

    /* Manually flip child to ZOMBIE without auto-reaping (kill
     * would auto-reap if orphan; child has parent so kill leaves
     * ZOMBIE, but we bypass it for clarity). */
    child->state = PROC_STATE_ZOMBIE;
    child->exit_code = 0;

    size_t reaped = process_reap_orphans();

    TEST("reap_orphans skips parented zombies");
    if (reaped == 0) PASS();
    else FAIL("reaped a parented zombie");

    TEST("parented zombie still in chain after sweep");
    if (parent->children == child &&
        child->state == PROC_STATE_ZOMBIE) PASS();
    else FAIL("parented zombie disturbed");
}

static void test_reap_orphans_destroys_orphan_zombies(void) {
    reset_world();
    struct process *a = process_create("z_a", 0, 0);
    struct process *b = process_create("z_b", 0, 0);
    struct process *alive = process_create("alive", 0, 0);
    if (!a || !b || !alive) { TEST("setup"); FAIL("create"); return; }

    /* Force two orphan zombies; leave `alive` in EMBRYO. */
    a->state = PROC_STATE_ZOMBIE;
    b->state = PROC_STATE_ZOMBIE;

    size_t reaped = process_reap_orphans();

    TEST("reap_orphans returns count of reaped slots");
    if (reaped == 2) PASS();
    else FAIL("expected 2 reaped");

    TEST("reaped orphan zombies become UNUSED");
    if (a->state == PROC_STATE_UNUSED &&
        b->state == PROC_STATE_UNUSED) PASS();
    else FAIL("zombies still present");

    TEST("alive process untouched by reaper");
    if (alive->state == PROC_STATE_EMBRYO) PASS();
    else FAIL("reaper touched alive process");

    TEST("process_count reflects only alive after sweep");
    if (process_count() == 1) PASS();
    else FAIL("count != 1");
}

static void test_reap_orphans_idempotent(void) {
    reset_world();
    struct process *a = process_create("z", 0, 0);
    if (!a) { TEST("setup"); FAIL("create"); return; }
    a->state = PROC_STATE_ZOMBIE;

    size_t first = process_reap_orphans();
    size_t second = process_reap_orphans();

    TEST("first sweep reaps the orphan zombie");
    if (first == 1) PASS();
    else FAIL("expected 1 from first sweep");

    TEST("second sweep is a no-op");
    if (second == 0) PASS();
    else FAIL("expected 0 from second sweep");
}

int test_process_destroy_run(void) {
    printf("[test_process_destroy]\n");
    tests_run = 0;
    tests_passed = 0;

    test_null_is_noop();
    test_unused_is_noop();
    test_destroy_embryo();
    test_destroy_zombie();
    test_idempotent();
    test_slot_reusable();

    /* Phase 6.5 process-tree linkage. */
    test_fork_links_child();
    test_fork_lifo_ordering();
    test_destroy_unlinks_from_parent();
    test_destroy_orphans_children();
    test_orphan_destroy_safe();

    /* Phase 6.6 zombie reaping. */
    test_kill_orphan_auto_reaps();
    test_kill_parented_stays_zombie();
    test_kill_signal_clamping();
    test_kill_unknown_pid();
    test_reap_orphans_empty();
    test_reap_orphans_skips_alive();
    test_reap_orphans_skips_parented_zombies();
    test_reap_orphans_destroys_orphan_zombies();
    test_reap_orphans_idempotent();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
