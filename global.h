#ifndef _GLOBAL_H_

#define _GLOBAL_H_

#include <signal.h>
#include <sys/time.h>
#include"protocal.h"
#define POT_ITEM_NUMBER 100

struct wait_item
{
	volatile int flag;
	int enable;
};

struct pot_item
{
	unsigned long page_start;
	//int status; /*0: public, 1: owned*/
	//
	page_state status;
	pid_t owner;
	struct wait_item waiter[10];
	int waiter_number;
};

typedef int spinlock_t;

#define SPIN_LOCK_UNLOCKED 0

extern inline void spin_lock(spinlock_t *);
extern inline void spin_unlock(spinlock_t *);
extern inline int spin_trylock(spinlock_t *);

extern int (* _libc_sigaction)(int, const struct sigaction *, struct sigaction *);
extern int (* _libc_gettimeofday)(struct timeval *, struct timezone *);
extern void (* _libc_exit)(int);

extern void protect_memory_init();
extern void protect_memory();

extern struct pot_item *pot_table;
extern spinlock_t *pot_lock;
extern unsigned long *pot_index;

#endif
