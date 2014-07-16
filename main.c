#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <dlfcn.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/user.h>

#include "global.h"
#include "protocal.h"

int mode;//0: record; 1: replay
unsigned int dstart, dend, dlenth;

struct pot_item *pot_table;
spinlock_t *pot_lock;
unsigned int *pot_index;

/*
	GLIBC Functions
*/
static int (* real__libc_start_main)(int (*) (int, char **, char **), int, char **, void (*)(void), void (*) (void), void (*) (void), void *) = NULL;
int (* _libc_sigaction)(int, const struct sigaction *, struct sigaction *) = NULL;
int (* _libc_gettimeofday)(struct timeval *, struct timezone *) = NULL;
void (* _libc_exit)(int) = NULL;

/*
	Init the libaray
*/
#define INIT_LIBC_ROUTINE do{ \
	real__libc_start_main = (int (*)(int (*) (int, char **, char **), int, char **, void (*)(void), void (*) (void), void (*) (void), void *)) \
		dlsym(RTLD_NEXT, "__libc_start_main"); \
	_libc_sigaction = (int (*)(int, const struct sigaction *, struct sigaction *))dlsym(RTLD_NEXT, "sigaction"); \
	_libc_gettimeofday = (int (*)(struct timeval *, struct timezone *))dlsym(RTLD_NEXT, "gettimeofday"); \
	_libc_exit = (void (*)(int))dlsym(RTLD_NEXT, "_exit"); \
}while(0);

void protect_memory_init()
{
	unsigned int i;

	mprotect((void *)dstart, dlenth, PROT_NONE);

	//fprintf (stderr, "**********pot item number: %d\n", *pot_index);

	for (i = dstart; i < dend; i+=PAGE_SIZE)
	{
		pot_table[*pot_index].page_start = i;
		pot_table[*pot_index].status = PUBLIC;
		pot_table[*pot_index].waiter_number = 0;
		(*pot_index)++;
	}

	//fprintf (stderr, "***********pot item number: %d\n", *pot_index);
}

void protect_memory ()
{
	mprotect((void *)dstart, dlenth, PROT_NONE);
}

#define ERROR_sig(context)	((context)->uc_mcontext.gregs[REG_ERR])
#define PF_PROT 1
#define PF_WRITE 2


static void page_fault_handler(int signum, siginfo_t *info, void *puc)
{
	unsigned long page_fault_addr = (unsigned long)info->si_addr;
	unsigned long page_start_addr = page_fault_addr & 0xfffff000;
	struct ucontext *uc = (struct ucontext *)puc;

	int error_code = ERROR_sig(uc);
	ac_type type;

	if(error_code & PF_WRITE){
		type = AC_WRITE;
	}
	else{
		type = AC_READ;
	}

	fprintf (stderr, "actype: %d \n[%d] fault page: %lx, instr addr: %x\n",type, getpid(), page_start_addr, uc->uc_mcontext.gregs[REG_EIP]);

	give_up_ownership(getpid());
	acquire_ownership(page_start_addr, getpid(), type);

	//fprintf (stderr, "[%d] fault page: %x, instr addr: %x, continue...\n", getpid(), page_start_addr, uc->uc_mcontext.gregs[REG_EIP]);
}

static void signal_init()
{
	struct sigaction act;

	sigfillset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = page_fault_handler;
	_libc_sigaction(SIGSEGV, &act, NULL);
}

static void read_mode_file()
{
	char mode_file[100];
	int fd;

	strcpy(mode_file, getenv("HOME"));
	strcat(mode_file, "/.mode");

	fd = open(mode_file, O_RDONLY, 0x0664);
	assert (fd != -1);

	read (fd, &mode, sizeof(int));
	//fprintf (stderr, "mode: %s\n", mode?"replay":"record");

	close(fd);

	unlink(mode_file);
}

static void share_file_init()
{
        FILE *fp = fopen("/proc/self/maps", "r");
        char exe[200] = "\0";
		void *temp;

        readlink("/proc/self/exe", exe, 200);
        strcat (exe, "\0");
        while (!feof (fp))
        {
                char xx[200];

                fgets(xx, 200, fp);
                if (strstr(xx, exe) && strstr(xx, "rw-p"))
                {
                        sscanf (xx, "%x-%x", &dstart, &dend);
                        break;
                }
        }
        fclose(fp);

	dlenth = dend - dstart;

	temp = mmap (NULL, dlenth, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert (temp != MAP_FAILED);

	memcpy (temp, (const void *)dstart, dlenth);

	munmap ((void *)dstart, dlenth);
	
	char share_file[100];
	int share_file_fd;
	strcpy(share_file, getenv("HOME"));
        strcat(share_file, "/.tmp");
	share_file_fd = open(share_file, O_CREAT | O_RDWR | O_TRUNC, 00777);
	assert (share_file_fd != -1);
	lseek (share_file_fd, dlenth, SEEK_SET);
	write (share_file_fd, " ", 1);
	assert(mmap ((void *)dstart, dlenth, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, share_file_fd, 0) != MAP_FAILED);

	assert (memcpy ((void *)dstart, (const void *)temp, dlenth));
	munmap (temp, dlenth);
	close (share_file_fd);
}

void pot_item_init()
{
	char share_file[100];
        int share_file_fd;
	long length;
	void *shared_region;

        strcpy(share_file, getenv("HOME"));
        strcat(share_file, "/.pottmp");
        share_file_fd = open(share_file, O_CREAT | O_RDWR | O_TRUNC, 00777);
        assert (share_file_fd != -1);

	length = sizeof(spinlock_t) + sizeof(struct pot_item) * POT_ITEM_NUMBER + sizeof(unsigned int);
        lseek (share_file_fd, length, SEEK_SET);
        write (share_file_fd, " ", 1);
        shared_region = mmap (NULL, dlenth, PROT_READ | PROT_WRITE, MAP_SHARED, share_file_fd, 0);
	assert (shared_region != MAP_FAILED);

	pot_lock = (spinlock_t *)shared_region;
	pot_index = (unsigned int *)((long)pot_lock + sizeof(spinlock_t));
	pot_table = (struct pot_item *)((long)pot_index + sizeof(unsigned int));

	*pot_lock = SPIN_LOCK_UNLOCKED;
	*pot_index = 0;

	close (share_file_fd);
}

int __libc_start_main(int (* main) (int, char **, char **),
		 int argc, char ** ubp_av, void (* init)(void),
		 void (* fini) (void), void (* rtld_fini) (void), void * stack_end)
{
	read_mode_file();

	INIT_LIBC_ROUTINE;
	signal_init();
	share_file_init();

	pot_item_init();

	//printf("in my libc_start_main\n");

	return real__libc_start_main(main, argc, ubp_av, init, fini, rtld_fini, stack_end);
}

