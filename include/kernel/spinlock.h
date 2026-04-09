#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <stdint.h>

struct spinlock {
  volatile uint32_t locked;
  uint32_t owner_cpu;
  uint64_t flags;
};

#define SPINLOCK_INIT {0, 0, 0}

static inline void spinlock_init(struct spinlock *lock) {
  lock->locked = 0;
  lock->owner_cpu = 0;
  lock->flags = 0;
}

void spin_lock(struct spinlock *lock);
void spin_unlock(struct spinlock *lock);
void spin_lock_irqsave(struct spinlock *lock, uint64_t *flags);
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags);
int spin_trylock(struct spinlock *lock);

struct mutex {
  volatile uint32_t locked;
  uint32_t owner_pid;
  struct task *wait_head;
  struct spinlock wait_lock;
};

#define MUTEX_INIT {0, 0, 0, SPINLOCK_INIT}

void mutex_init(struct mutex *m);
void mutex_lock(struct mutex *m);
void mutex_unlock(struct mutex *m);
int mutex_trylock(struct mutex *m);

struct semaphore {
  volatile int32_t count;
  struct spinlock lock;
  struct task *wait_head;
};

void semaphore_init(struct semaphore *s, int32_t initial);
void semaphore_down(struct semaphore *s);
void semaphore_up(struct semaphore *s);
int semaphore_trydown(struct semaphore *s);

struct rwlock {
  volatile int32_t readers;
  volatile uint32_t writer;
  struct spinlock lock;
  struct task *write_wait;
  struct task *read_wait;
};

void rwlock_init(struct rwlock *rw);
void rwlock_read_lock(struct rwlock *rw);
void rwlock_read_unlock(struct rwlock *rw);
void rwlock_write_lock(struct rwlock *rw);
void rwlock_write_unlock(struct rwlock *rw);

#endif /* KERNEL_SPINLOCK_H */
