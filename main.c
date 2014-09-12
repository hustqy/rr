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
#include<ucontext.h>

#include "global.h"
#include "protocal.h"

int mode;//0: record; 1: replay
unsigned long dstart[100],dlenth[100];
int n_shared;

struct pot_item *pot_table;
spinlock_t *pot_lock;
unsigned long *pot_index;

extern void * share_malloc_data();

/*
	GLIBC Functions
*/
static int (* real__libc_start_main)(int (*) (int, char **, char **), int, char **, void (*)(void), void (*) (void), void (*) (void), void *) = NULL;
int (* _libc_sigaction)(int, const struct sigaction *, struct sigaction *) = NULL;
int (* _libc_gettimeofday)(struct timeval *, struct timezone *) = NULL;
void (* _libc_exit)(int) = NULL;
int (*_libc_open)(const char *, int, ... ) = NULL;
ssize_t (*_libc_read)(int, void *, size_t) = NULL;
off_t (* _libc_lseek)(int, off_t, int) = NULL;
int (* _libc_close)(int) = NULL;
ssize_t (* _libc_write)(int, const void *, size_t) = NULL;

/*
	Init the libaray
*/
#define INIT_LIBC_ROUTINE do{ \
	real__libc_start_main = (int (*)(int (*) (int, char **, char **), int, char **, void (*)(void), void (*) (void), void (*) (void), void *)) \
		dlsym(RTLD_NEXT, "__libc_start_main"); \
	_libc_sigaction = (int (*)(int, const struct sigaction *, struct sigaction *))dlsym(RTLD_NEXT, "sigaction"); \
	_libc_gettimeofday = (int (*)(struct timeval *, struct timezone *))dlsym(RTLD_NEXT, "gettimeofday"); \
	_libc_exit = (void (*)(int))dlsym(RTLD_NEXT, "_exit"); \
	_libc_open = (int (*) (const char *path, int oflag, ... ))dlsym(RTLD_NEXT, "open");\
	_libc_read = (ssize_t (*)(int fildes, void *buf, size_t nbyte))dlsym(RTLD_NEXT, "read");\
	_libc_lseek = (off_t (*)(int fildes, off_t offset, int whence))dlsym(RTLD_NEXT, "lseek");\
	_libc_close = (int (*)(int fildes))dlsym(RTLD_NEXT, "close");\
	_libc_write = (ssize_t (*)(int fildes, const void *buf, size_t nbyte))dlsym(RTLD_NEXT, "write");\
}while(0);

void protect_memory_init()
{

	//protect_memory();
		mprotect((void*)dstart[0], dlenth[0], PROT_NONE);
	
	int j;
	unsigned long i;

	for(j = 0; j < n_shared; ++j){
		for(i = dstart[j]; i < dstart[j]+dlenth[j]; i+=PAGE_SIZE){
			pot_table[*pot_index].page_start = i;
			pot_table[*pot_index].status = PUBLIC;
			pot_table[*pot_index].waiter_number = 0;
			(*pot_index)++;
			fprintf (stderr, "**********pot item number: %ld\n", *pot_index);
		}
	}

	//fprintf (stderr, "***********pot item number: %d\n", *pot_index);
}

void protect_memory ()
{
	int i;
	fprintf(stderr,"in protect memory: n_shared = %d\n",n_shared);
	for(i=0;i<n_shared;++i){
		mprotect((void*)dstart[i], dlenth[i], PROT_NONE);
	}
}


#define ERROR_sig(context)	((context)->uc_mcontext.gregs[REG_ERR])
#define RIP_sig(context)	((context)->uc_mcontext.gregs[REG_RIP])
#define PF_PROT 1
#define PF_WRITE 2


static void page_fault_handler(int signum, siginfo_t *info, void *puc)
{
	unsigned long page_fault_addr = (unsigned long)info->si_addr;
	unsigned long page_start_addr = page_fault_addr & 0xfffff000;

	if(!is_in_pot(page_start_addr)){
		mprotect((void*)page_start_addr, 0x1000, PROT_READ|PROT_WRITE);
		return;
	}

	struct ucontext *uc = (struct ucontext *)puc;

	int error_code = ERROR_sig(uc);
	ac_type type;

	if(error_code & PF_WRITE){
		type = AC_WRITE;
	}
	else{
		type = AC_READ;
	}

	fprintf (stderr, "actype: %d \n[%d] fault page: %lx, instr addr: %p\n",type, getpid(), page_start_addr, (void *)RIP_sig(uc));

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
	printf("end of signal_init\n");
}

static void read_mode_file()
{
	char mode_file[100];
	int fd;

	strcpy(mode_file, getenv("HOME"));
	strcat(mode_file, "/.mode");
	
	fd = _libc_open(mode_file, O_RDONLY, 00664);

	assert (fd != -1);

	_libc_read(fd, &mode, sizeof(int));
	fprintf (stderr, "mode: %s\n\n", mode?"replay":"record");

	_libc_close(fd);

	unlink(mode_file);
	printf("end of read_mode_file\n");
}

static int find_globals(){

	//printf("begin of find_globals\n");
	FILE *fp = fopen("/proc/self/maps", "r");
	assert(NULL != fp);
    char exe[200] = "\0";
	unsigned long dend;
	unsigned long max_len= 0;
                                                                                    
	//lvxiao: for debug
	//printf("sharing globals******************\n");
                                                                                    
    readlink("/proc/self/exe", exe, 200);
    strcat (exe, "\0");
	char blank[2]= "[";

	n_shared = 0;

	int ign_flag = 0; 
		//to indicate if a bss section should be ignored
		//0:don't ignore; 1:ign
    while (!feof (fp))
    {
            char xx[200];
            fgets(xx, 200, fp);
            
			//if (strstr(xx, exe) && strstr(xx, "rw-p"))
			//ignore globals in libxxx.so, syslib, bss, and stack
            if (!strstr(xx, blank) && strstr(xx, "rw-p"))
            {
				if (strstr(xx,"-2.19.so") || strstr(xx, "libxxx.so")){
					ign_flag = 1; //ign syslib,libxxx and their bss
					continue;
				}else if (strstr(xx,"00:00")){//on anonymous block
						if (ign_flag)continue;
					}
				else{
					ign_flag = 0; //app or other .so
				}
                sscanf (xx, "%lx-%lx", &dstart[n_shared], &dend);
				dlenth[n_shared] = dend - dstart[n_shared];
				if(dlenth[n_shared] > max_len){
					max_len = dlenth[n_shared];
				}
				n_shared++;

            }
    }
    fclose(fp);
	printf("end of find_globals, max_len = %lx\n",max_len);
	return max_len;
}

static void share_file_init()
{
	//1. find globals, store their place in array dstart,dlength
	//   and return  max_len, to tell how large a tmp is needed
	
	unsigned long max_len = find_globals();
	printf("my pid = [%d]\n",getpid());
	sleep(10);

	//2. mmap a tmp block enough for all shared sections
	
	void *temp;
	temp = mmap (NULL, max_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert (temp != MAP_FAILED);

	
	//3. open a file to map globals as shared

	char share_file[100];
	int share_file_fd;
	strcpy(share_file, getenv("HOME"));
    	strcat(share_file, "/.tmp");

	//share_file_fd = _libc_open(share_file, O_CREAT | O_RDWR | O_TRUNC, 00777);
	//assert (share_file_fd != -1);

	//4. for each section to be shared, backup,share, and restore
	
	int i;

		//printf("n_shared = %d \n", n_shared);
	for(i=0;i<n_shared;++i){ 
		//printf("i = %d, dstart = %lx\n", i, dstart[i]);

		memcpy (temp, (const void *)dstart[i], dlenth[i]);
		munmap ((void *)dstart[i], dlenth[i]);
		
		//share_file_fd = _libc_open(share_file, O_CREAT | O_RDWR | O_TRUNC, 00777);
		//lseek (share_file_fd, dlenth[i], SEEK_SET);
		//write (share_file_fd, " ", 1);
	
		//assert(mmap ((void *)dstart[i], dlenth[i], PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, share_file_fd, 0) != MAP_FAILED);
		assert(mmap ((void *)dstart[i], dlenth[i], PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED|MAP_ANON, -1, 0) != MAP_FAILED);
		assert (memcpy ((void *)dstart[i], (const void *)temp, dlenth[i]));
		//printf("i = %d, dstart = %lx\n", i, dstart[i]);
	
		//close (share_file_fd);
	}
	//assert(0);
	
	//5.unmap the tmp

	munmap (temp, dlenth[0]);
	printf("end of share_file_init\n");
}

void pot_item_init()
{
	char share_file[100];
        int share_file_fd;
	long length;
	void *shared_region;

        strcpy(share_file, getenv("HOME"));
        strcat(share_file, "/.pottmp");
        share_file_fd = _libc_open(share_file, O_CREAT | O_RDWR | O_TRUNC, 00777);
        assert (share_file_fd != -1);

	length = sizeof(spinlock_t) + sizeof(struct pot_item) * POT_ITEM_NUMBER + sizeof(unsigned int);
    lseek (share_file_fd, length, SEEK_SET);
    write (share_file_fd, " ", 1);
    shared_region = mmap (NULL, dlenth[0], PROT_READ | PROT_WRITE, MAP_SHARED, share_file_fd, 0);
	assert (shared_region != MAP_FAILED);

	pot_lock = (spinlock_t *)shared_region;
	pot_index = (unsigned long*)((long)pot_lock + sizeof(spinlock_t));
	pot_table = (struct pot_item *)((long)pot_index + sizeof(unsigned long));

	*pot_lock = SPIN_LOCK_UNLOCKED;
	*pot_index = 0;

	close (share_file_fd);
}

int is_in_pot(unsigned long addr){

	int i;
	struct pot_item *pitem;
	for(i = 0; i < *pot_index; ++i){
		pitem = &pot_table[i];
		if(addr == pitem->page_start)
			return 1;
		else continue;
	}
	return 0;
}

int __libc_start_main(int (* main) (int, char **, char **),
		 int argc, char ** ubp_av, void (* init)(void),
		 void (* fini) (void), void (* rtld_fini) (void), void * stack_end)
{
	INIT_LIBC_ROUTINE;
	read_mode_file();

	
	share_file_init();

	pot_item_init();
	signal_init();

    /*added by bwtang, init the rr_malloc for rr_system*/
    void *start_malloc_addr = share_malloc_data();

	//printf("end of my libc_start_main\n");
	//sleep(10);

	return real__libc_start_main(main, argc, ubp_av, init, fini, rtld_fini, stack_end);
}
