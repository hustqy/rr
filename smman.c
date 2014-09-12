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

#define PAGESIZEORDER                       12

#define ZONENUM                             16

#ifdef __x86_64__
    #ifdef __rrself__
        // 64M
        #define MMAPAREASIZE                67108864
        // lg(64M/4K)+1
        #define BMPARRAYMAX                 15
        // 64M
        #define BRKAREASIZE                 67108864
        // 4K*(2*16+1)
        #define INFOAREAINITSIZE            135168
    #else
        // 1G
        #define MMAPAREASIZE                1073741824
        // lg(1G/4K)+1
        #define BMPARRAYMAX                 19
        // 64M
        #define BRKAREASIZE                 67108864
        // 4K*(129*16+1)
        #define INFOAREAINITSIZE            8458240
    #endif
#elif __i386__
    #ifdef __rrself__
        // 4M
        #define MMAPAREASIZE                4194304
        // lg(4M/4K)+1
        #define BMPARRAYMAX                 11
        // 4M
        #define BRKAREASIZE                 4194304
        // 4K*(1*16+1)
        #define INFOAREAINITSIZE            69632
    #else
        // 64M
        #define MMAPAREASIZE                67108864
        // lg(64M/4K)+1
        #define BMPARRAYMAX                 15
        // 64M
        #define BRKAREASIZE                 67108864
        // 4K*(2*16+1)
        #define INFOAREAINITSIZE            135168
    #endif
#endif


//
#define ALIGNMASK                           (4096-1)

//
#define ALIGNSIZE(req)                      ( ((req) + ALIGNMASK) & ~ALIGNMASK )
//
#define BIT2ADDR(base_addr,bit_index)       ( base_addr + (bit_index>>3) )
//
#define BITMAPTEST(addr,bit)                ( *(char *)addr & (1<<(7-bit)) )
//
#define BITMAPSET(addr,bit)                 { *(char *)addr = *(char *)addr ^ (1<<(7-bit)); }
//
#define ADDR2ZONENUM(addr,start_addr,size)  ( (addr-start_addr)/size )
//
#define ADDR2MAPNUM(addr,order)             ( \
    !(addr & ((1<<(order+PAGESIZEORDER))-1)) ? addr>>(order+PAGESIZEORDER) : -1 )
//
#define BUDDYNUM(num)                       (num%2? num-1: num+1)


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
    spinlock_t          zlock;
    __mzone_info        mzone_info;
} __lmzone_info;

//
typedef struct{
    taddr               start_sbrk;
    taddr               sbrk_now;
    taddr               end_sbrk;
} __heap_info;


/*should be init in the libc_start_main*/

//
static __lmzone_info    *lmzone_info;
//
static __heap_info      *heap_info;
//
static taddr            *mmap_start_addr;
//
static spinlock_t       *sbrk_lock;


static  void              chunkbmp_init(taddr info_area_addr, size_t psize, __lmzone_info *zone);

static  int               find_chunk(int horder, int lorder, __lmzone_info *zone);

static  int               search_pos(__lmzone_info *zone, int order, int pos);

static  void			 *find_zone(size_t apply_size, __lmzone_info *zone);

static  void              mem_free(void *ptr, size_t length, __lmzone_info *zone);

static  void 			 *mem_alloc(size_t apply_size, __lmzone_info *zone);

static void               set_chunkbmp(__lmzone_info *zone, int order, int pos, int increase);



taddr    sminit(void);

void    *smmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

int      smunmap(void *addr, size_t length);

void    *ssbrk(int increment);

void    *smremap(void *old_address, size_t old_size, size_t new_size, int flags);


//---------------------code begin---------------------
//
static void chunkbmp_init(taddr info_area_addr, size_t psize, __lmzone_info *zone){
    //the alloc_start_addr must be on the page_margin; the psize must be page_size times;
    spin_lock(&(zone->zlock));
    
    int i = BMPARRAYMAX-1;
    for(;i>=0;i--){
        ((zone->mzone_info).chunk_bmp[i]).order_bmp = (char *)info_area_addr;
        ((zone->mzone_info).chunk_bmp[i]).total_num = 1<<(BMPARRAYMAX-1-i);
        ((zone->mzone_info).chunk_bmp[i]).free_num = 0;
        ((zone->mzone_info).chunk_bmp[i]).pos = 0;
        info_area_addr += (((zone->mzone_info).chunk_bmp[i]).total_num+7)/8;   
    }

    (zone->mzone_info.chunk_bmp[BMPARRAYMAX-1]).free_num = 1;

    spin_unlock(&(zone->zlock));
}

static void *mem_alloc(size_t apply_size, __lmzone_info *zone){
	void *ret_val = NULL;

	int order, need_order;
    order = need_order = order_match(apply_size);

    spin_lock( &(zone->zlock) );

    int num = 0;
    while(!num && need_order<BMPARRAYMAX ){
        num = ((zone->mzone_info).chunk_bmp[need_order]).free_num;
        if(num)
            break;
        (need_order)++;
    }
    if(need_order != BMPARRAYMAX){
    	ret_val = (void *)( (zone->mzone_info).zalloc_start_addr + \
    		find_chunk(need_order, order, zone) * (1<<(order+PAGESIZEORDER)) );		//enough space 
    }

    spin_unlock( &(zone->zlock) );

    return ret_val;
}

static int find_chunk(int horder, int lorder, __lmzone_info *zone){
    int pos = ((zone->mzone_info).chunk_bmp[horder]).pos;
    taddr base_addr = (taddr)(((zone->mzone_info).chunk_bmp[horder]).order_bmp);

    if( horder != (BMPARRAYMAX-1) && horder == lorder ){
        int pos_buddy = BUDDYNUM(pos);
        if( !BITMAPTEST(BIT2ADDR(base_addr,pos_buddy),(pos_buddy%8)) )
            pos = pos_buddy;
        else
            pos = search_pos(zone, horder, pos);
    }
    else{
        if(horder == BMPARRAYMAX-1)
            pos = 0;
        else
            pos = search_pos(zone, horder, pos);

        int j;
        for( j=horder-1; j>=lorder; j--){
            pos *= 2;
            set_chunkbmp(zone, j, pos, 1);
        }
    }

    set_chunkbmp(zone, horder, pos, -1);
    return pos;
}

static int search_pos(__lmzone_info *zone, int order, int pos){
    taddr base_addr = (taddr)(((zone->mzone_info).chunk_bmp[order]).order_bmp);
    int num = ((zone->mzone_info).chunk_bmp[order]).total_num;

    int i;
    for(i=pos+1; i<(pos+num); i+=2){
        i = i%num;
        int buddy_num = BUDDYNUM(i);
        if( !BITMAPTEST(BIT2ADDR(base_addr,i),(i%8)) && \
            BITMAPTEST(BIT2ADDR(base_addr,buddy_num),(buddy_num%8)) )
            return i;
        else if( BITMAPTEST(BIT2ADDR(base_addr,i),(i%8)) && \
            !BITMAPTEST(BIT2ADDR(base_addr,buddy_num),(buddy_num%8)) )
            return i+1;
        else
            continue;
    }
}

static void set_chunkbmp(__lmzone_info *zone, int order, int pos, int increase){
    taddr base_addr = (taddr)(((zone->mzone_info).chunk_bmp[order]).order_bmp);
    ((zone->mzone_info).chunk_bmp[order]).free_num += increase;
    ((zone->mzone_info).chunk_bmp[order]).pos = pos;
    BITMAPSET(BIT2ADDR(base_addr,pos),(pos%8));
}

static void mem_free(void *ptr, size_t length, __lmzone_info *zone){
    int order, num, buddy_num;
    taddr base_addr;

    spin_lock(&(zone->zlock));

    order = order_match(length);
    num = ADDR2MAPNUM((taddr)ptr-((zone->mzone_info).zalloc_start_addr), order);

    do{
        base_addr = (taddr)(((zone->mzone_info).chunk_bmp[order]).order_bmp);
        BITMAPSET(BIT2ADDR(base_addr,num),(num%8));
        ((zone->mzone_info).chunk_bmp[order]).free_num++;

        buddy_num = (num%2? num-1: num+1);

        if(BITMAPTEST(BIT2ADDR(base_addr,buddy_num),(buddy_num%8)))
            break;

        order++;
        num >>= 1;
    }while(order < BMPARRAYMAX-1);

    spin_unlock(&(zone->zlock));
}


//
taddr sminit(void){
    /*the main process should allocate a area to save these data 
    so that they can be shared by all processes*/
    taddr info_addr = (taddr)mmap(NULL, INFOAREAINITSIZE, PROT_READ | PROT_WRITE, \
        MAP_SHARED | MAP_ANON, -1, 0);
    if(info_addr == 0){
        printf("Mmap sm_data_addr failed! \n");
        return -1;
    }
    else{
    	taddr tmp_ptr = info_addr;

        mmap_start_addr = (taddr *)tmp_ptr;
        tmp_ptr += TADDRSIZE;

        sbrk_lock = (spinlock_t *)tmp_ptr;
        tmp_ptr += SPINLOCKT;

		heap_info = (__heap_info *)tmp_ptr;
        tmp_ptr += sizeof(__heap_info);

        lmzone_info = (__lmzone_info *)tmp_ptr;
    }

    /*smptr is the start address shared by all processes,
    the sm_total_size save it's size*/
    heap_info->start_sbrk = (taddr)mmap(NULL, BRKAREASIZE, PROT_READ | PROT_WRITE, \
        MAP_SHARED | MAP_ANON, -1, 0);
    if(heap_info->start_sbrk == 0){
        printf("Mmap sm_alloc_addr failed! \n");
        return -1;
    }
    else{
        heap_info->sbrk_now = heap_info->start_sbrk;
        heap_info->end_sbrk = heap_info->start_sbrk + BRKAREASIZE;
    }

    /*Shared memory will be divided to two parts like real process memory distribution,
    one is heap area and the other is mmap area. start_sbrk is the start address of heap 
    area,mmap*/

    /**/
    *mmap_start_addr = (taddr)mmap(NULL, MMAPAREASIZE, PROT_READ | PROT_WRITE, \
        MAP_SHARED | MAP_ANON, -1, 0);
    if(*mmap_start_addr == 0){
        printf("Mmap sm_alloc_addr failed! \n");
        return -1;
    }
    else{
        taddr bmp_info_addr = info_addr + 4096;
        int i;
        for( i = 0; i < ZONENUM; i++){
            (lmzone_info[i].mzone_info).zalloc_start_addr = *mmap_start_addr + i*MMAPAREASIZE;
            (lmzone_info[i].mzone_info).zsize = MMAPAREASIZE;
            chunkbmp_init(bmp_info_addr + i*8192, MMAPAREASIZE, &(lmzone_info[i]));
        }
    }

    return *mmap_start_addr;
}

//
void *smmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset){
    //argu one *addr,argu three prot,argu four flags,argu five fd,argu six offset are useless malloc 
    void *ret_val = NULL;

    if(length > MMAPAREASIZE)
        return ret_val;

    int rand_zone_try = 0;
    int zone_index = 0;
    int eflag[ZONENUM] = {0};
    srand(getpid());

    while(ret_val == NULL){
    	int i;
    	if(rand_zone_try < ZONENUM){
    		zone_index = rand()%(ZONENUM - 1);
    		rand_zone_try++;
    		eflag[zone_index] = 1;
    	}
    	else{
    		for(i = 0;i < ZONENUM;i++){
    			if(eflag[i] == 0){
    				eflag[i] = 1;
    				zone_index = i;
    				break;
    			}
    		}
    	}

    	if(i == ZONENUM){
    		zone_index = -1;
    		break;
    	}
    	else{
    		ret_val = mem_alloc(length, &(lmzone_info[zone_index]));
    	}
    }

    return ret_val;  
}

//
int smunmap(void *addr, size_t length){
    //printf("sunmap!!!!!!\n");
    if( (taddr)addr < *mmap_start_addr || (taddr)addr > (*mmap_start_addr) + MMAPAREASIZE )
        return -1;
    else{
        int i;
        i = ADDR2ZONENUM((taddr)addr, *mmap_start_addr, MMAPAREASIZE);

        spin_lock(&(lmzone_info[i].zlock));
        mem_free(addr, length, &(lmzone_info[i]));
        spin_unlock(&(lmzone_info[i].zlock));

        return 0;
    }
}

//
void *ssbrk(int increment){
	spin_lock(sbrk_lock);

	taddr tmp,ret_addr;
    ret_addr = heap_info->sbrk_now;

    tmp = increment>0? ret_addr+(taddr)increment: ret_addr-(taddr)(-increment);
    if(tmp < heap_info->end_sbrk && tmp >= heap_info->start_sbrk)
        heap_info->sbrk_now = tmp;
    else{
        printf("Increment overstep the brk boundry! \n");
        ret_addr = 0;
    }

    spin_unlock(sbrk_lock);

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
            memcpy(tmp_addr, old_address, old_size);

            int ret;
            if(ret=smunmap(old_address, old_size) == -1){
                printf("Can't smunmap the old space!\n");
                return NULL;
            }
            else{
                new_addr = smmap(0, new_size, PROT_READ | PROT_WRITE, \
                    MAP_SHARED | MAP_ANON, -1, 0);
                memcpy(new_addr, tmp_addr, old_size);
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