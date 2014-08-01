#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sched.h>

#include <sys/user.h>
#include <sys/mman.h>

#include "global.h"
#include "protocal.h"

//typedef enum { PUBLIC, SHARED_READ, OWNED_WRITE }page_state;
//typedef enum { AC_READ, AC_WRITE }ac_type;
typedef enum{false,true} bool;

bool crew_prot(page_state ini_state, ac_type askfor_prio){

	switch(ini_state){
	
		case PUBLIC: 
			return true;
		case SHARED_READ:
			if(AC_READ == askfor_prio)return true;
			else return false;
		case OWNED_WRITE:
			return false;
		default:
			printf("ERROR: illegal page state: %d",ini_state);
			assert(0);
	}
}


void acquire_ownership (unsigned long page_start_addr, pid_t pid, ac_type type)
{
	struct wait_item *pwaiter;
	int i;

//retry:
//
	while(1){
	pwaiter = NULL;

	spin_lock (pot_lock);

	for (i = 0; i < *pot_index; i++)
	{
		struct pot_item *pitem = &pot_table[i];


		if (pitem->page_start != page_start_addr)
			continue;
		
		//fprintf (stderr, "[%d] fault addr: %lx, item addr: %lx\n", pid, page_start_addr, pitem->page_start);

		if(crew_prot(pitem->status,type))
		{

			printf("page %lx is %d\n",page_start_addr,pitem->status);

			pitem->owner = pid;

			if(AC_WRITE == type){
				mprotect((void *)page_start_addr, PAGE_SIZE, PROT_READ | PROT_WRITE);
				pitem->status = OWNED_WRITE;
			}else{
				mprotect((void *)page_start_addr, PAGE_SIZE, PROT_READ);
				pitem->status = SHARED_READ;
			}

			fprintf (stderr, "[%d] got page %lx\n", pid,page_start_addr);

			spin_unlock (pot_lock);

			return;
		}
		else
		{
			fprintf (stderr, "[%d] waiting \n", pid);
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

	}//end of while
	//goto retry;
}

void give_up_ownership (pid_t pid)
{
	int i;

	spin_lock (pot_lock);

	for (i = 0; i < *pot_index; i++)
	{
		struct pot_item *pitem = &pot_table[i];
		int j;

		if (pitem->status == PUBLIC)
			continue;

		if (pitem->owner != pid)
			continue;

		pitem->status = PUBLIC;
		mprotect((void *)pitem->page_start, PAGE_SIZE, PROT_NONE);

		printf("[%d]:give up page %p\n",pid,(void*)pitem->page_start);

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
