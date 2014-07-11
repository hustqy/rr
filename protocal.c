#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sched.h>

#include <sys/user.h>
#include <sys/mman.h>

#include "global.h"

void acquire_ownership (unsigned long page_start_addr, pid_t pid, int type)
{
	struct wait_item *pwaiter;
	int i;

retry:
	pwaiter = NULL;

	spin_lock (pot_lock);

	for (i = 0; i < *pot_index; i++)
	{
		struct pot_item *pitem = &pot_table[i];

		fprintf (stderr, "[%d] fault addr: %x, item addr: %x\n", pid, page_start_addr, pitem->page_start);

		if (pitem->page_start != page_start_addr)
			continue;

		if (pitem->status == 0)
		{
			pitem->status = 1;
			pitem->owner = pid;

			mprotect((void *)page_start_addr, PAGE_SIZE, PROT_READ | PROT_WRITE);

			spin_unlock (pot_lock);

			return;
		}
		else
		{
			pwaiter = &pitem->waiter[pitem->waiter_number];

			assert (pitem->owner != pid);
			pwaiter->flag = 1;
			pwaiter->enable = 1;
			pitem->waiter_number++;
			
			spin_unlock (pot_lock);

			break;
		}
	}

	assert (pwaiter);

	while (pwaiter->flag)
	{
		sched_yield();
	}

	goto retry;
}

void give_up_ownership (pid_t pid)
{
	int i;

	spin_lock (pot_lock);

	for (i = 0; i < *pot_index; i++)
	{
		struct pot_item *pitem = &pot_table[i];
		int j;

		if (pitem->status == 0)
			continue;

		if (pitem->owner != pid)
			continue;

		pitem->status = 0;
		mprotect((void *)pitem->page_start, PAGE_SIZE, PROT_NONE);

		for (j = 0; j < pitem->waiter_number; j++)
		//for (j = pitem->waiter_number-1; j >= 0; j--)
		{
			if (0 == pitem->waiter[j].enable)
				continue;

			pitem->waiter[j].enable = 0;
			pitem->waiter[j].flag = 0;
			break;
		}
	}

	spin_unlock (pot_lock);
}
