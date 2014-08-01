#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/mman.h>

#define BUF_SIZ 0x1000
#define MAP_SIZE 0x10000
#define memevent_size   sizeof(mem_event)
#define libfunc_size  sizeof(libc_e)
#define PAGE_PUBLIC 0
#define PAGE_OWNED_WRITE 1 n
#define PAGE_SHARED_READ 2
typedef unsigned int  uint32;
typedef unsigned long ulong;
typedef int int32;
char*buf;
int buf_index=0;
int COUNT=0;
static int fd=-1;
static off_t current_offset=0;
static void *buf_ptr=NULL;
static void *buf_last_ptr=NULL;
 int isread[5000];
int isfirstaddr=1;
#define STR_MAX 128
enum {
	GETTIMEOFDAY = 1,
};
enum {
	PAGE_EVENT= 1,
	FUNC_EVENT,
};
char LOG_file[100];
typedef struct {
int32 event_type;
int32 type; 
int32 pid;
ulong page_start_addr;
}mem_event;

typedef struct libcfunc_event {
    int32 event_type;
	int32 func_id;
	int32 arg_num;
	pid_t pid;
	size_t ret_size;
	char *retval;
}libc_e;

void Log_page_event(mem_event*tmp)
{
  if(buf_index+memevent_size>=BUF_SIZ)
  { 
	write (fd, buf, buf_index);
	memset(buf,0,BUF_SIZ);
	buf_index = 0;	

  }

memcpy(buf+buf_index,tmp,memevent_size);
buf_index+=memevent_size;

}

void Log_func_event(libc_e *tmp)
{
 size_t ret_size=tmp->ret_size;
 if(buf_index+libfunc_size-sizeof(char*)+ret_size>=BUF_SIZ)
  { 
    
	write (fd, buf, buf_index);
	memset(buf,0,BUF_SIZ);
	buf_index = 0;	
  
  }
memcpy(buf+buf_index,tmp,libfunc_size-sizeof(char*));
buf_index+=libfunc_size-sizeof(char*);
memcpy(buf+buf_index,tmp->retval,ret_size);
buf_index+=ret_size;

}


void LOG(void*tmp)
{

COUNT++;
if(*(int32*)tmp==PAGE_EVENT)
   Log_page_event((mem_event*)tmp);
else
   Log_func_event((libc_e*)tmp);
}


static void init(char *file_name){
     fd = open(file_name, O_RDONLY); 
            if(fd==-1){
                
	         printf("Can not open file \n");
                exit(EXIT_FAILURE);
            }
            buf_ptr = mmap(NULL, MAP_SIZE, PROT_READ, MAP_SHARED, fd, current_offset);
            if(buf_ptr==MAP_FAILED){
                perror("BUF map init: ");
                exit(EXIT_FAILURE);
            }
            buf_last_ptr = (void*)((long long)(buf_ptr)+MAP_SIZE);
			current_offset+=MAP_SIZE;
        }
		
		
mem_event* query_addr_in_buffer(ulong page_start_addr)
{
 char* data_ptr=buf;
 int i=0;
 mem_event* mem_info=(mem_event*)malloc(memevent_size);
 libc_e  *libc_info=(libc_e*)malloc(libfunc_size-sizeof(char*));
while((*(int32*)data_ptr)!=0)
 {
  if(*(int32*)data_ptr==PAGE_EVENT)
    { 
       memcpy(mem_info,data_ptr,memevent_size);
	  
	   if(mem_info->page_start_addr==page_start_addr&&isread[i]==0)
	   {
	       printf("The%x:event_type:%d,pageman_type=%d,page_start_addr=%x,pid=%d\n",page_start_addr,mem_info->event_type,mem_info->type,mem_info->page_start_addr,mem_info->pid);
		  isread[i]=1;
	      return mem_info;
	   }
	   else
	   data_ptr+=memevent_size; 
	}
    else
     {  
	  memcpy(libc_info,data_ptr,libfunc_size-sizeof(char*));
	  //printf("event_type:%d,func_id=%d,arg_num=%x,pid=%d,ret_size=%d,",libc_info->event_type,libc_info->func_id,libc_info->arg_num,libc_info->pid,libc_info->ret_size);
      data_ptr=data_ptr+libfunc_size-sizeof(char*);
	  char*ret=(char*)malloc(libc_info->ret_size);
	  memcpy(ret,data_ptr,libc_info->ret_size);
	  //printf("ret=%s\n",ret);
	  data_ptr+=libc_info->ret_size;
     }
i++;
}
}
mem_event* query_addr_in_file(ulong page_start_addr)
{
   int i=0,j=0;
   char*data_ptr=NULL;
   data_ptr=(char*)buf_ptr;
   void* nbuf_ptr=buf_ptr;
   void* nbuf_last_ptr =buf_last_ptr;
   off_t new_current_offset=current_offset;
   mem_event* mem_info=(mem_event*)malloc(memevent_size);
   libc_e  *libc_info=(libc_e*)malloc(libfunc_size-sizeof(char*));
  while((*(int32*)data_ptr)!=0)
  {  
    i++;
	if((char*)nbuf_last_ptr<=data_ptr+100)
	 {
	  printf("map!");
	  if(MAP_FAILED==(nbuf_ptr=mmap(nbuf_last_ptr, 0x1000, PROT_READ, MAP_SHARED|MAP_FIXED, fd, new_current_offset)))
	    {
	     printf("The memory mapping failure!");
	    }
      nbuf_last_ptr = (void*)((long long)(nbuf_ptr)+0x1000);
	  new_current_offset+=0x1000;
	 }
    
	if(*(int32*)data_ptr==PAGE_EVENT)
    {
	
	   memcpy(mem_info,data_ptr,memevent_size);
	  //printf("%d:event_type:%d,pageman_type=%d,page_start_addr=%x,pid=%d\n",++j,mem_info->event_type,mem_info->type,mem_info->page_start_addr,mem_info->pid);
	   if(mem_info->page_start_addr==page_start_addr&&isread[i]==0)
	   {
	       printf("The%x:event_type:%d,pageman_type=%d,page_start_addr=%x,pid=%d\n",page_start_addr,mem_info->event_type,mem_info->type,mem_info->page_start_addr,mem_info->pid);
		   isread[i]=1;
	       return mem_info;
	   }
	   else
	   data_ptr+=memevent_size;
	   
     }
    else
     {  
	  memcpy(libc_info,data_ptr,libfunc_size-sizeof(char*));
	  //printf("%d:event_type:%d,func_id=%d,arg_num=%x,pid=%d,ret_size=%d,",++j,libc_info->event_type,libc_info->func_id,libc_info->arg_num,libc_info->pid,libc_info->ret_size);
      data_ptr=data_ptr+libfunc_size-sizeof(char*);
	  char*ret=(char*)malloc(libc_info->ret_size);
	  memcpy(ret,data_ptr,libc_info->ret_size);
	  //printf("ret=%s\n",ret);
	  data_ptr+=libc_info->ret_size;
     }

   }  
  return NULL;

}
mem_event* query_addr(ulong page_start_addr)
{
  FILE *fp;
  mem_event*mem_e_info;
  if(isfirstaddr)
   {
   init(LOG_file);
   isfirstaddr=0;
   }
  if((fp=fopen(LOG_file,"r"))==NULL)
	{
		printf("fopen file erro!\n");
		exit(0);
	}
	
	else if(fgetc(fp)==EOF)
	{
	   printf("The LOG_file is null!serch in buffer...\n");
	   fclose(fp);
	   mem_e_info=query_addr_in_buffer(page_start_addr);
	}
	else
	{
	 if((mem_e_info=query_addr_in_file(page_start_addr))==NULL)
	 {
	   printf("In Buffer...\n");
	   mem_e_info=query_addr_in_buffer(page_start_addr);
	 }
	}
  return mem_e_info;
}
  

 
int32 main()
{   
    
     if(NULL==(buf=malloc(BUF_SIZ)))
	{
	printf("malloc error!\n");
	
	}
	
    buf_index=0;
    memset(buf,0,BUF_SIZ);
	strcpy (LOG_file, getenv("HOME"));
    strcat (LOG_file, "/.LOG");
    fd = open(LOG_file, O_CREAT | O_RDWR , 00664);
	assert (fd != -1);
	 //record  LIBC_EVENT
    struct timeval tv;
	struct timezone  tz;
    char* retval=NULL;
	if(NULL==(retval=(char*)malloc(STR_MAX)))
    { 
     printf("malloc erro!");
    } 
	int32 ret =gettimeofday(&tv, &tz);
    sprintf(retval, "%d, %d, %d, %d, %d", ret, (ret == -1) ? -1: tv.tv_sec, (ret == -1) ? -1: tv.tv_usec, (ret == -1) ? -1: tz.tz_minuteswest, (ret == -1) ? -1: tz.tz_dsttime);
    libc_e *libc_info_w=(libc_e*)malloc(libfunc_size);
    
    libc_info_w->retval=retval;
    libc_info_w->event_type=FUNC_EVENT;
    libc_info_w->func_id = GETTIMEOFDAY;
    libc_info_w->pid = 2345;
    libc_info_w->arg_num = 4;
	
    libc_info_w->ret_size=sizeof(tv)+sizeof(tz)+sizeof(int32);
	
	
	//record PGGE_EVENT
	mem_event *mem_e_info=(mem_event*)malloc(memevent_size);
    mem_e_info->event_type=PAGE_EVENT;
    mem_e_info->type=PAGE_PUBLIC;
    mem_e_info->page_start_addr=0x12345;
    mem_e_info->pid=2345;
	mem_event *mem_e_info_new1=(mem_event*)malloc(memevent_size);
    mem_e_info_new1->event_type=PAGE_EVENT;
    mem_e_info_new1->type=PAGE_PUBLIC;
    mem_e_info_new1->page_start_addr=0x12345;
    mem_e_info_new1->pid=8888;
	mem_event *mem_e_info_new=(mem_event*)malloc(memevent_size);
    mem_e_info_new->event_type=PAGE_EVENT;
    mem_e_info_new->type=PAGE_PUBLIC;
    mem_e_info_new->page_start_addr=0x123;
    mem_e_info_new->pid=12345678;

	for(int i=0;i<100;i++)
	{
	
	LOG((void*)libc_info_w);
	LOG((void*)mem_e_info);
	
	}
    LOG((void*)mem_e_info_new);
	query_addr(0x12345);
	query_addr(0x12345);
	query_addr(0x123);
	close(fd);
	
    //query_addr(0x12345);
}
