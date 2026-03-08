#ifndef _SYNC_UTILS_H_
#define _SYNC_UTILS_H_

// spinlock using RISC-V amoswap
typedef struct {
  volatile int locked;
} spinlock_t;

static inline void spinlock_init(spinlock_t *lk) {
  lk->locked = 0;
}

static inline void spinlock_lock(spinlock_t *lk) {
  while (1) {
    int old;
    asm volatile("amoswap.w.aq %0, %1, (%2)"
                 : "=r"(old)
                 : "r"(1), "r"(&lk->locked)
                 : "memory");
    if (old == 0) break;
  }
}

static inline void spinlock_unlock(spinlock_t *lk) {
  int zero = 0;
  asm volatile("amoswap.w.rl %0, %1, (%2)"
               : "=r"(zero)
               : "r"(0), "r"(&lk->locked)
               : "memory");
}

static inline void sync_barrier(volatile int *counter, int all) {
  int local;
  asm volatile("amoadd.w %0, %2, (%1)\n"
               : "=r"(local)
               : "r"(counter), "r"(1)
               : "memory");
  if (local + 1 < all) {
    do {
      asm volatile("lw %0, (%1)\n" : "=r"(local) : "r"(counter) : "memory");
    } while (local < all);
  }
}

#endif