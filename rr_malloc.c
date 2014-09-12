#include "smsys.h"
#include <fcntl.h>
#include <dlfcn.h>
#include <assert.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif


void *  (*cf_malloc)(size_t);
void	(*cf_free)(void *);
void *	(*cf_realloc)(void *, size_t);
taddr   (*cf_sminit)();

spinlock_t *rr_malloc_lock = NULL;

void *share_malloc_data(){
	void *handle = dlopen("./malloc.so", RTLD_NOW | RTLD_NODELETE);
	assert(handle != NULL);

	taddr data_begin_addr, data_end_addr, text_begin_addr, text_end_addr;

    FILE *fp = fopen("/proc/self/maps", "r");
    assert(fp != NULL);
	char buf[1024];
	while(!feof(fp)){
		fgets(buf, 1024, fp);
		
		if(strstr(buf, "malloc.so")){
			if(strstr(buf, "rw"))
				sscanf (buf, "%lx-%lx", &data_begin_addr, &data_end_addr);
		}
	}

    void *tmp = mmap(0, data_end_addr - data_begin_addr, \
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);

    memcpy(tmp, (void *)data_begin_addr, data_end_addr - data_begin_addr);

	void *mmaped_addr = mmap((void *)data_begin_addr, data_end_addr - data_begin_addr, \
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED | MAP_ANON, -1, 0);

    memcpy((void *)data_begin_addr, tmp, data_end_addr - data_begin_addr);

    //printf("mmaped_addr is %lx, addr is %lx-%lx \n", (taddr)mmaped_addr, data_begin_addr, data_end_addr);

    cf_sminit = dlsym(handle, "sminit");
	cf_malloc = dlsym(handle, "malloc");
	cf_free = dlsym(handle, "free");
	cf_realloc = dlsym(handle, "realloc");

    rr_malloc_lock = (spinlock_t *)mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);

    return (void *)(cf_sminit());
}

void *rr_malloc(size_t size){\
    spin_lock(rr_malloc_lock);
	void *ret = cf_malloc(size);
    spin_unlock(rr_malloc_lock);
    return ret;
}

void rr_free(void *ptr){
    spin_lock(rr_malloc_lock);
	cf_free(ptr);
    spin_unlock(rr_malloc_lock);
}

void *rr_reallloc(void *ptr, size_t size){
    spin_lock(rr_malloc_lock);
	void *ret = cf_realloc(ptr, size);
    spin_unlock(rr_malloc_lock);
    return ret;
}


#ifdef __cplusplus
};
#endif