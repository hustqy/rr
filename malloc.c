/*Interface function:
void        __sminit    ( void );
void    *   __smalloc   ( size_t size );
coid        __smfree    ( void *ptr );

The size of shared memory is definited by macro TESTSIZE,you don't need to init it;

It's just for the single process without spin_lock;

When you compile it by gcc,don't set arguments '-O2' or '-O3';
*/

#ifndef _SMALLOC
#define _SMALLOC

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__STDC__) || defined (__cplusplus)
# include <stddef.h>
#else
# undef  size_t
# define size_t          unsigned int
#endif

#include <sys/mman.h>

#define ALIGNMASK (4096-1)
#define PAGESIZEORDER 12
#define MAXSIZEORDER 31     //max_size >= page_size
#define BMPARRAYMAX (MAXSIZEORDER-PAGESIZEORDER+1)
#define TESTSIZE 4096*1024

#define ALIGNSIZE(req) ( ((req) + ALIGNMASK) & ~ALIGNMASK )
#define BITMAPTEST(addr,bit) ( *(char *)addr & (1<<(7-bit)) )
#define BITMAPSET(addr,bit) ( *(char *)addr ^ (1<<(7-bit)) )
#define ADDR2MAPNUM(addr,order) ( \
    !(addr & ((1<<(order+PAGESIZEORDER))-1)) ? addr>>(order+PAGESIZEORDER) : -1 )

typedef unsigned long int taddr;

typedef struct{
    char *order_bmp;
    int total_num;
    int free_num;
    int pos;
} bmp;


static bmp chunk_bmp[BMPARRAYMAX];
static int real_order;
static taddr sm_info_addr;
static taddr sm_alloc_addr;
static size_t sm_size;

static int order_match(size_t size);
static int find_chunk(int horder, int lorder);
static taddr chunkbmp_init(taddr info_area_addr, taddr alloc_start_addr, size_t psize);
static void *mem_alloc(taddr alloc_start_addr, size_t apply_size);
static void mem_free(taddr alloc_start_addr, void *ptr);
void *__smalloc(size_t size);
void __sminit(void);
void __smfree(char *ptr, size_t size);

static int order_match(size_t size){
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

static int find_chunk(int horder, int lorder){
    int pos = chunk_bmp[horder].pos;
    int num = chunk_bmp[horder].total_num;
    taddr base_addr = (taddr)chunk_bmp[horder].order_bmp;
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

    chunk_bmp[horder].free_num--;
    chunk_bmp[horder].pos = i;
    *(char *)bmp_addr = BITMAPSET(bmp_addr,i);

    for( j=horder-1; j>=lorder; j--){
        i *= 2;
        chunk_bmp[j].free_num++;
        chunk_bmp[j].pos = i;
        bmp_addr = (taddr)chunk_bmp[j].order_bmp + (i>>3);
        *(char *)bmp_addr = BITMAPSET(bmp_addr,(i%8));
    }
    return i;
}

static taddr chunkbmp_init(taddr info_area_addr, taddr alloc_start_addr, size_t psize){
    //the alloc_start_addr must be on the page_margin; the psize must be page_size times;
    int order = order_match(psize);
    real_order = order;
    chunk_bmp[order].free_num = 1;
    int i = order;
    for(;i>=0;i--){
        chunk_bmp[i].order_bmp = (char *)info_area_addr;
        chunk_bmp[i].total_num = 1<<(order-i);
        chunk_bmp[i].free_num = 0;
        chunk_bmp[i].pos = 0;
        info_area_addr += (chunk_bmp[i].total_num+7)/8;       
    }
    chunk_bmp[order].free_num++;
    taddr alloc_end_addr = alloc_start_addr+(1<<(order+PAGESIZEORDER));
    return alloc_end_addr; 
}

void __sminit(void){
    sm_alloc_addr = (taddr)mmap(NULL, TESTSIZE, PROT_WRITE|PROT_READ, MAP_SHARED|MAP_ANON, -1, 0);
    if(sm_alloc_addr == (taddr)NULL)
        printf("mmap sm_alloc_addr failed!\n");
    else
        sm_size = TESTSIZE;
    sm_info_addr = (taddr)mmap(NULL, 2*4096, PROT_WRITE|PROT_READ, MAP_SHARED|MAP_ANON, -1, 0);
    if(sm_alloc_addr == (taddr)NULL)
        printf("mmap sm_info_addr failed!\n");

    taddr ret = chunkbmp_init(sm_info_addr, sm_alloc_addr, sm_size);
    printf("init succeed!\n");
}

static void *mem_alloc(taddr alloc_start_addr, size_t apply_size){
    if(apply_size>(1<<MAXSIZEORDER) )
        return NULL;
    int order, need_order;
    order = need_order = order_match(apply_size);
    int num = 0;
    while(!num & need_order<BMPARRAYMAX ){
        num = chunk_bmp[need_order].free_num;
        if(num)
            break;
        need_order++;
    }
    if(need_order == BMPARRAYMAX)
        return NULL;
    return (void *)( \
        alloc_start_addr + find_chunk(need_order, order)*(1<<(order+PAGESIZEORDER)) );
}

void *__smalloc(size_t size){
    return mem_alloc(sm_alloc_addr,size);
}

static void mem_free(taddr alloc_start_addr, void *ptr){
    int order = 0;
    taddr bmp_addr;
    int num, buddy_num;
    do{
        num = ADDR2MAPNUM(((taddr)ptr-alloc_start_addr), order);
        bmp_addr = (taddr)(chunk_bmp[order].order_bmp + (num>>3));
        *(char *)bmp_addr = BITMAPSET(bmp_addr,num);
        buddy_num = (num%2? num-1: num+1);
        if(BITMAPTEST(bmp_addr,buddy_num)){
            chunk_bmp[order].free_num++;
            break;
        }
        else
            chunk_bmp[order].free_num--;
        order++;
    }
    while(num!=-1 && order<=real_order);
}

void __smfree(char *ptr, size_t size){
    mem_free(sm_alloc_addr,(void *)ptr);
}

#ifdef __cplusplus
};
#endif

#endif
