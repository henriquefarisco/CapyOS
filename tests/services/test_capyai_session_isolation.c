#include <stdio.h>
#include <string.h>

#include "auth/session.h"
#include "fs/vfs.h"
#include "kernel/task.h"

static struct task *current_task;
static int failures;

#define CHECK(condition, label)                                               \
  do {                                                                        \
    if (condition) printf("  ok   %s\n", label);                            \
    else { printf("  FAIL %s\n", label); failures++; }                    \
  } while (0)

struct task *task_current(void) { return current_task; }

void user_preferences_set_defaults(struct user_preferences *prefs) {
  if (!prefs) return;
  memset(prefs, 0, sizeof(*prefs));
  strcpy(prefs->language, "pt-BR");
}
const char *user_preferences_language(const struct user_preferences *prefs) {
  return prefs && prefs->language[0] ? prefs->language : "pt-BR";
}
int user_prefs_load(const struct user_record *user,
                    struct user_preferences *out) {
  (void)user; (void)out; return 0;
}
int vfs_lookup(const char *path, struct dentry **out) {
  (void)path; if (out) *out = NULL; return -1;
}

int main(void) {
  struct session_context boot, alice, bob;
  struct task task_a, task_b;
  printf("[test_capyai_session_isolation]\n");
  memset(&boot, 0, sizeof(boot));
  memset(&alice, 0, sizeof(alice));
  memset(&bob, 0, sizeof(bob));
  memset(&task_a, 0, sizeof(task_a));
  memset(&task_b, 0, sizeof(task_b));

  current_task = NULL;
  session_set_active(&boot);
  CHECK(session_active() == &boot,
        "bootstrap principal remains available before the scheduler");

  current_task = &task_a;
  session_set_active(&alice);
  current_task = &task_b;
  session_set_active(&bob);
  CHECK(session_active() == &bob,
        "worker B observes only its own authenticated principal");
  current_task = &task_a;
  CHECK(session_active() == &alice,
        "worker A principal is unchanged by worker B");
  current_task = NULL;
  CHECK(session_active() == &boot,
        "task-local changes do not overwrite bootstrap identity");

  strcpy(alice.assistant_last_file, "private.txt");
  alice.assistant_turns = 9u;
  session_reset(&alice);
  CHECK(alice.assistant_last_file[0] == '\0' && alice.assistant_turns == 0u,
        "logout reset clears per-session CapyAI memory");

  if (failures) return 1;
  printf("[test_capyai_session_isolation] all passed\n");
  return 0;
}
