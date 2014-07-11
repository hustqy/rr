#include "global.h"

static inline int testandset (int *p)
{
    long int readval = 0;

    __asm__ __volatile__ ("lock; cmpxchgl %2, %0"
                          : "+m" (*p), "+a" (readval)
                          : "r" (1)
                          : "cc");
    return readval;
}

inline void spin_lock(spinlock_t *lock)
{
    while (testandset(lock));
}

inline void spin_unlock(spinlock_t *lock)
{
    *lock = 0;
}

inline int spin_trylock(spinlock_t *lock)
{
    return !testandset(lock);
}
