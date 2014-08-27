#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <string.h>
#include <stdarg.h>

#include "smsys.h"
#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif


#define TADDRSIZE                           sizeof(taddr)
#define SIZETSIZE                           sizeof(size_t)
#define SPINLOCKT                           sizeof(spinlock_t)
#define INTSIZE                             sizeof(int)
#define PIDSIZE                             sizeof(pid_t)


//
#define MMAPAREASIZE                        1024*1024*64
//
#define ZONENUM                             16
//
#define BRKAREASIZE                         1024*1024*4
//
#define PRONUMMAX                           64
//
#define TOTALTSIZE                          MMAPAREASIZE * ZONENUM + BRKAREASIZE * PRONUMMAX

//
#define PAGESIZEORDER                       12
//
#define BMPARRAYMAX                         (26-PAGESIZEORDER+1)
//
#define ALIGNMASK                           (4096-1)

//
#define ALIGNSIZE(req)                      ( ((req) + ALIGNMASK) & ~ALIGNMASK )
//
#define BITMAPTEST(addr,bit)                ( *(char *)addr & (1<<(7-bit)) )
//
#define BITMAPSET(addr,bit)                 ( *(char *)addr ^ (1<<(7-bit)) )
//
#define ADDR2ZONENUM(addr,start_addr,size)  ( (addr-start_addr)/size )
//
#define ADDR2MAPNUM(addr,order)             ( \
    !(addr & ((1<<(order+PAGESIZEORDER))-1)) ? addr>>(order+PAGESIZEORDER) : -1 )


//
inline int order_match(size_t size){
    size = ALIGNSIZE(size)>>PAGESIZEORDER;
    size_t tmp = size;
    int order = 0;
    while(tmp){
        order++;
        tmp >>= 1;
    }
    if( !(size & (1<<(order-1))-1) )
        order--;
    return order; 
}


//
typedef struct{
    char                *order_bmp;
    int                 total_num;
    int                 free_num;
    int                 pos;
} bmp;

//
typedef struct{
    taddr               zalloc_start_addr;
    size_t              zsize;
    bmp                 chunk_bmp[BMPARRAYMAX];
} __mzone_info;

//
typedef struct{
    pid_t               now_pid;
    spinlock_t          zlock;
    __mzone_info        mzone_info;
} __lmzone_info;

//
typedef struct{
    taddr               start_sbrk;
    taddr               sbrk_now;
    pid_t               pid;
    int                 index_relocate;
} __mheap_info;


/*should be init in the libc_start_main*/
//
static void             *smptr;
static size_t           *sm_total_size;

//
static __lmzone_info    *lmzone_info;
//
static __mheap_info     *mheap_info;
//
static taddr            *mmap_start_addr;
//
static size_t           *mmap_total_size;
//
static pid_t            *init_pid;
//
static int              *pid_num;


static  void chunkbmp_init(taddr info_area_addr, size_t psize, __mzone_info *now_zone);

static  int find_chunk(int horder, int lorder, __lmzone_info *now_lzone);

static  __lmzone_info *find_zone(pid_t now_pid, size_t apply_size, int *need_order);

static  int size_enough(__lmzone_info *now_lzone, int *need_order);

static  void mem_free(void *ptr, __mzone_info *now_zone);

static int heap_exist(pid_t pid);

static int empty_heap(pid_t pid);

static int find_heap();


int     sminit(void);

void    *smmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

int     smunmap(void *addr, size_t length);

void    *ssbrk(int increment);

void    *smremap(void *old_address, size_t old_size, size_t new_size, int flags);


static void chunkbmp_init(taddr info_area_addr, size_t psize, __mzone_info *now_zone){
    //the alloc_start_addr must be on the page_margin; the psize must be page_size times;
    int order = order_match(psize);
    (now_zone->chunk_bmp[order]).free_num = 1;
    
    int i = BMPARRAYMAX-1;
    for(;i>=0;i--){
        if(i<=order){
            (now_zone->chunk_bmp[i]).order_bmp = (char *)info_area_addr;
            (now_zone->chunk_bmp[i]).total_num = 1<<(order-i);
            (now_zone->chunk_bmp[i]).free_num = 0;
            (now_zone->chunk_bmp[i]).pos = 0;
            info_area_addr += ((now_zone->chunk_bmp[i]).total_num+7)/8;
        }
        else{
            (now_zone->chunk_bmp[i]).order_bmp = NULL;
            (now_zone->chunk_bmp[i]).total_num = 0;
            (now_zone->chunk_bmp[i]).free_num = -1;
            (now_zone->chunk_bmp[i]).pos = 0;
        }       
    }
    (now_zone->chunk_bmp[order]).free_num++; 
}

static __lmzone_info *find_zone(pid_t now_pid, size_t apply_size, int *need_order){
    int i = 0;
    while(lmzone_info[i].now_pid != now_pid && i<ZONENUM){
        if(lmzone_info[i].now_pid != 0) 
            i++;
        else
            break;
    }

    int j;
    for(j=i; j < (i+ZONENUM); j++){
        j = j%ZONENUM;
        if( size_enough(&(lmzone_info[j]), need_order) )
            break;
    }

    spin_lock( &(lmzone_info[j].zlock) );
    lmzone_info[j].now_pid = now_pid;
    spin_unlock( &(lmzone_info[j].zlock) );

    return &(lmzone_info[j]);
}

static int size_enough(__lmzone_info *now_lzone, int *need_order){
    int num = 0;

    spin_lock( &(now_lzone->zlock) );

    while(!num & (*need_order)<BMPARRAYMAX ){
        num = ((now_lzone->mzone_info).chunk_bmp[(*need_order)]).free_num;
        if(num)
            break;
        (*need_order)++;
    }

    spin_unlock( &(now_lzone->zlock) );

    if(*need_order == BMPARRAYMAX)
        return 0;       //no enough space
    else
        return 1;       //enough space 
}

static int find_chunk(int horder, int lorder, __lmzone_info *now_lzone){
    spin_lock(&(now_lzone->zlock));

    int pos = ((now_lzone->mzone_info).chunk_bmp[horder]).pos;
    int num = ((now_lzone->mzone_info).chunk_bmp[horder]).total_num;
    taddr base_addr = (taddr)(((now_lzone->mzone_info).chunk_bmp[horder]).order_bmp);
    taddr bmp_addr;
    int mapbyte;
    register int i,j;
    for( i=pos; i<(pos+num); i++ ){
        i = i%num;
        mapbyte = i>>3;
        bmp_addr = base_addr + mapbyte;
        int mapbit = i%8;
        if(!BITMAPTEST(bmp_addr,mapbit))
            break;
    }
    ((now_lzone->mzone_info).chunk_bmp[horder]).free_num--;
    ((now_lzone->mzone_info).chunk_bmp[horder]).pos = i;
    *(char *)bmp_addr = BITMAPSET(bmp_addr,i);

    for( j=horder-1; j>=lorder; j--){
        i *= 2;
        ((now_lzone->mzone_info).chunk_bmp[j]).free_num++;
        ((now_lzone->mzone_info).chunk_bmp[j]).pos = i;
        bmp_addr = (taddr)(((now_lzone->mzone_info).chunk_bmp[j]).order_bmp) + (i>>3);
        *(char *)bmp_addr = BITMAPSET(bmp_addr,(i%8));
    }

    spin_unlock(&(now_lzone->zlock));

    return i;
}

static void mem_free(void *ptr, __mzone_info *now_zone){
    int order = 0;
    taddr bmp_addr;
    int num, buddy_num;

    do{
        num = ADDR2MAPNUM((taddr)ptr-(now_zone->zalloc_start_addr), order);
        bmp_addr = (taddr)((now_zone->chunk_bmp[order]).order_bmp + (num>>3));
        *(char *)bmp_addr = BITMAPSET(bmp_addr,num);
        (now_zone->chunk_bmp[order]).free_num++;

        buddy_num = (num%2? num-1: num+1);

        if(BITMAPTEST(bmp_addr,buddy_num)){
            (now_zone->chunk_bmp[order]).free_num++;
            break;
        }

        order++;
    }while(num!=-1);
}

static int heap_exist(pid_t pid){
    int i = 0;
    for(;i<PRONUMMAX;i++){
        if(mheap_info[i].pid == pid)
            return i;
    }
    if(i == PRONUMMAX)
        return -1;
}

static int empty_heap(pid_t pid){
    if((*pid_num)+1 <= PRONUMMAX){
        int i;
        for(i = 0;i < PRONUMMAX;i++){
            if(mheap_info[i].pid == 0){
                mheap_info[i].pid = pid;
                (*pid_num)++;
                break;
            }
        }
        return i;
    }
    else{
        printf("Number of precess exceed the max number!\n");
        return -1;
    }
}

static int find_heap(){
    pid_t now_pid = getpid();

    int index, ret;

    if(index = (int)(now_pid - *init_pid) < 0){
        printf("Process id error!\n");
        return -1;
    }
    else if(index > PRONUMMAX){
        ret=heap_exist(now_pid);
        if(ret == -1)
            return empty_heap(now_pid);
        else
            return ret;
    }
    else{
        ret=heap_exist(now_pid);
        if(ret == -1){
            return empty_heap(now_pid);
        }
        else{
            return ret;
        }
    }

}


//
int sminit(void){
    /*the main process should allocate a area to save these data 
    so that they can be shared by all processes*/
    //printf("sminit!!!!!!\n");
    taddr tmp_ptr = (taddr)mmap(NULL, 8192, PROT_READ | PROT_WRITE, \
        MAP_SHARED | MAP_ANON, -1, 0);
    if(tmp_ptr == 0){
        printf("Mmap sm_data_addr failed! \n");
        return -1;
    }
    else{
        mmap_start_addr = (taddr *)tmp_ptr;
        tmp_ptr += 3 * TADDRSIZE;

        sm_total_size = (size_t *)tmp_ptr;
        mmap_total_size = (size_t *)(tmp_ptr + SIZETSIZE);
        tmp_ptr += 2 * SIZETSIZE;

        lmzone_info = (__lmzone_info *)tmp_ptr;
        tmp_ptr += ZONENUM * sizeof(__lmzone_info);

        mheap_info = (__mheap_info *)tmp_ptr;
        tmp_ptr += PRONUMMAX * sizeof(__mheap_info);

        pid_num = (int *)tmp_ptr;
        tmp_ptr += INTSIZE;

        init_pid = (pid_t *)tmp_ptr;
        tmp_ptr += PIDSIZE;
    }

    /*smptr is the start address shared by all processes,
    the sm_total_size save it's size*/
    smptr = (void *)mmap(NULL, TOTALTSIZE, PROT_READ | PROT_WRITE, \
        MAP_SHARED | MAP_ANON, -1, 0);
    if(smptr == NULL){
        printf("Mmap sm_alloc_addr failed! \n");
        return -1;
    }
    else{
        *sm_total_size = TOTALTSIZE;

        *init_pid = getpid();
        *pid_num = 1;

        int j;
        for(j=0;j<PRONUMMAX;j++){
            mheap_info[j].start_sbrk = mheap_info[j].sbrk_now = (taddr)smptr + j*((taddr)BRKAREASIZE);
            mheap_info[j].pid = 0;
            mheap_info[j].index_relocate = 0;
        }
        mheap_info[0].pid = *init_pid;

        *mmap_total_size = MMAPAREASIZE * ZONENUM;
        *mmap_start_addr = (taddr)smptr + (taddr)(TOTALTSIZE - *mmap_total_size);
    }

    /*Shared memory will be divided to two parts like real process memory distribution,
    one is heap area and the other is mmap area. start_sbrk is the start address of heap 
    area,mmap*/

    /**/
    taddr bmp_info_addr = (taddr)(void *)mmap(NULL, 4096*ZONENUM, PROT_READ | PROT_WRITE, \
        MAP_SHARED | MAP_ANON, -1, 0);
    if(bmp_info_addr == 0){
        printf("Mmap sm_bmp_info_addr failed! \n");
        return -1;
    }
    else{
        int i;
        for(i=0;i<ZONENUM;i++){
            lmzone_info[i].now_pid = 0;
            (lmzone_info[i].mzone_info).zalloc_start_addr = *mmap_start_addr + i*MMAPAREASIZE;
            (lmzone_info[i].mzone_info).zsize = MMAPAREASIZE;
            chunkbmp_init(bmp_info_addr + i*4096, MMAPAREASIZE, &(lmzone_info[i].mzone_info));
        }
    }

    
    return 0;
}

//
void *smmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset){
    //argu one *addr,argu three prot,argu four flags,argu five fd,argu six offset are useless malloc  
    if(length > MMAPAREASIZE)
        return NULL;

    int order, need_order;
    order = need_order = order_match(length);
    
    __lmzone_info *free_lzone = find_zone(getpid(), length, &need_order);

    return (void *)( (free_lzone->mzone_info).zalloc_start_addr + \
        find_chunk(need_order, order, free_lzone) * (1<<(order+PAGESIZEORDER)) );  
}

//
int smunmap(void *addr, size_t length){
    //printf("sunmap!!!!!!\n");
    if( (taddr)addr < *mmap_start_addr || (taddr)addr > (*mmap_start_addr)+(*mmap_total_size) )
        return -1;
    else{
        int i;
        i = ADDR2ZONENUM((taddr)addr, *mmap_start_addr, MMAPAREASIZE);

        spin_lock(&(lmzone_info[i].zlock));
        mem_free(addr, &(lmzone_info[i].mzone_info));
        spin_unlock(&(lmzone_info[i].zlock));

        return 0;
    }
}

//
void *ssbrk(int increment){
    int index = find_heap();
    if(index == -1)
        return NULL;

    taddr tmp,ret_addr;
    ret_addr = mheap_info[index].sbrk_now;

    tmp = increment>0? ret_addr+(taddr)increment: ret_addr-(taddr)(-increment);
    if(tmp < *mmap_start_addr && tmp >= mheap_info[index].start_sbrk)
        mheap_info[index].sbrk_now = tmp;
    else{
        printf("Increment overstep the brk boundry! \n");
        return NULL;
    }

    return (void *)(ret_addr);
}

//
void *smremap(void *old_address, size_t old_size, size_t new_size, int flags){
    void *new_addr = NULL;

    if(flags == 1){
        void *tmp_addr = smmap(0, old_size, PROT_READ | PROT_WRITE, \
            MAP_SHARED | MAP_ANON, -1, 0);
        if(tmp_addr == NULL){
            printf("Can't copy tmp page!\n");
            return NULL;
        }
        else{
            memcopy(tmp_addr, old_address, old_size);

            int ret;
            if(ret=smunmap(old_address, old_size) == -1){
                printf("Can't smunmap the old space!\n");
                return NULL;
            }
            else{
                new_addr = smmap(0, new_size, PROT_READ | PROT_WRITE, \
                    MAP_SHARED | MAP_ANON, -1, 0);
                memcopy(new_addr, tmp_addr, old_size);
                return new_addr;
            }
        }
    }
    else{
        printf("argument can't be dealed!\n");
        return NULL;
    }
}


#ifdef __cplusplus
};
#endif