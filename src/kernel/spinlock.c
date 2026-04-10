#include "kernel/spinlock.h"
#include "kernel/task.h"
#include <stddef.h>

static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }

static inline uint64_t save_flags(void) {
  uint64_t flags;
  __asm__ volatile("pushfq; popq %0" : "=r"(flags));
  return flags;
}

static inline void restore_flags(uint64_t flags) {
  __asm__ volatile("pushq %0; popfq" : : "r"(flags));
}

void spin_lock(struct spinlock *lock) {
  while (__sync_lock_test_and_set(&lock->locked, 1)) {
    while (lock->locked) {
      __asm__ volatile("pause");
    }
  }
}

void spin_unlock(struct spinlock *lock) {
  __sync_lock_release(&lock->locked);
}

void spin_lock_irqsave(struct spinlock *lock, uint64_t *flags) {
  *flags = save_flags();
  cli();
  spin_lock(lock);
  lock->flags = *flags;
}

void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
  spin_unlock(lock);
  restore_flags(flags);
}

int spin_trylock(struct spinlock *lock) {
  return __sync_lock_test_and_set(&lock->locked, 1) == 0 ? 0 : -1;
}

/* --- Mutex --- */

void mutex_init(struct mutex *m) {
  m->locked = 0;
  m->owner_pid = 0;
  m->wait_head = NULL;
  spinlock_init(&m->wait_lock);
}

void mutex_lock(struct mutex *m) {
  while (__sync_lock_test_and_set(&m->locked, 1)) {
    struct task *t = task_current();
    if (t) {
      spin_lock(&m->wait_lock);
      t->next = m->wait_head;
      m->wait_head = t;
      spin_unlock(&m->wait_lock);
      t->state = TASK_STATE_BLOCKED;
      t->wait_channel = m;
      task_yield();
    } else {
      while (m->locked) __asm__ volatile("pause");
    }
  }
  struct task *cur = task_current();
  m->owner_pid = cur ? cur->pid : 0;
}

void mutex_unlock(struct mutex *m) {
  m->owner_pid = 0;
  __sync_lock_release(&m->locked);

  spin_lock(&m->wait_lock);
  struct task *waiter = m->wait_head;
  if (waiter) {
    m->wait_head = waiter->next;
    waiter->next = NULL;
    waiter->state = TASK_STATE_READY;
    waiter->wait_channel = NULL;
  }
  spin_unlock(&m->wait_lock);
}

int mutex_trylock(struct mutex *m) {
  if (__sync_lock_test_and_set(&m->locked, 1) == 0) {
    struct task *cur = task_current();
    m->owner_pid = cur ? cur->pid : 0;
    return 0;
  }
  return -1;
}

/* --- Semaphore --- */

void semaphore_init(struct semaphore *s, int32_t initial) {
  s->count = initial;
  spinlock_init(&s->lock);
  s->wait_head = NULL;
}

void semaphore_down(struct semaphore *s) {
  for (;;) {
    spin_lock(&s->lock);
    if (s->count > 0) {
      s->count--;
      spin_unlock(&s->lock);
      return;
    }
    struct task *t = task_current();
    if (t) {
      t->next = s->wait_head;
      s->wait_head = t;
      t->state = TASK_STATE_BLOCKED;
      t->wait_channel = s;
      spin_unlock(&s->lock);
      task_yield();
    } else {
      spin_unlock(&s->lock);
      __asm__ volatile("pause");
    }
  }
}

void semaphore_up(struct semaphore *s) {
  spin_lock(&s->lock);
  s->count++;
  struct task *waiter = s->wait_head;
  if (waiter) {
    s->wait_head = waiter->next;
    waiter->next = NULL;
    waiter->state = TASK_STATE_READY;
    waiter->wait_channel = NULL;
  }
  spin_unlock(&s->lock);
}

int semaphore_trydown(struct semaphore *s) {
  spin_lock(&s->lock);
  if (s->count > 0) {
    s->count--;
    spin_unlock(&s->lock);
    return 0;
  }
  spin_unlock(&s->lock);
  return -1;
}

/* --- Read-Write Lock --- */

void rwlock_init(struct rwlock *rw) {
  rw->readers = 0;
  rw->writer = 0;
  spinlock_init(&rw->lock);
  rw->write_wait = NULL;
  rw->read_wait = NULL;
}

void rwlock_read_lock(struct rwlock *rw) {
  for (;;) {
    spin_lock(&rw->lock);
    if (!rw->writer) {
      rw->readers++;
      spin_unlock(&rw->lock);
      return;
    }
    spin_unlock(&rw->lock);
    __asm__ volatile("pause");
  }
}

void rwlock_read_unlock(struct rwlock *rw) {
  spin_lock(&rw->lock);
  if (rw->readers > 0) rw->readers--;
  spin_unlock(&rw->lock);
}

void rwlock_write_lock(struct rwlock *rw) {
  for (;;) {
    spin_lock(&rw->lock);
    if (!rw->writer && rw->readers == 0) {
      rw->writer = 1;
      spin_unlock(&rw->lock);
      return;
    }
    spin_unlock(&rw->lock);
    __asm__ volatile("pause");
  }
}

void rwlock_write_unlock(struct rwlock *rw) {
  spin_lock(&rw->lock);
  rw->writer = 0;
  spin_unlock(&rw->lock);
}
