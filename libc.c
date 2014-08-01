#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "global.h"
#include "protocal.h"

#include <sys/time.h>
#include <string.h>

#define IA32

typedef unsigned int uint32;
typedef int int32;

#define wrapper_gettimeofday gettimeofday

extern int mode;

enum {
	GETTIMEOFDAY = 1,


};

#define STR_MAX 128
typedef struct libcfunc_event {
	int32 func_id;
	pid_t pid;
	int32 arg_num;
	char retval[];
}libc_e;


void _exit (int status)
{
	pid_t pid = getpid();

	//fprintf (stderr, "[%d] exit...\n", pid);

	give_up_ownership (pid);

	printf("[%d] is exiting now\n");
	_libc_exit (status);

	assert(0);
	_exit (status);
}

#if 1
void write_libcfunc_log(libc_e *libc_info) {
	char *libcfunc_info = (char *)malloc(STR_MAX);
	sprintf(libcfunc_info, "%d, %d, %d, %s\n", libc_info->func_id, libc_info->pid, libc_info->arg_num, libc_info->retval);
	puts(libcfunc_info);
	free(libcfunc_info);
}

char *query_libcfunc(int32 func_id, pid_t pid) {
	static char tmp[] = "0, 1405083227, 519493, -480, 0";
	return tmp;


}
#endif
int32 wrapper_gettimeofday(struct timeval *tp, struct timezone *tzp) {
	//puts("invoking gettimeofday~~~");
	pid_t pid = getpid();
	give_up_ownership(pid);

	//puts("invoking gettimeofday~~~");
	struct timeval tv;
	struct timezone tz;	

	char *retval = (char *)malloc(STR_MAX); // order: ret, tv_sec, tv_usec, tz_minuteswest, tz_dsttime
	
	int32 ret = _libc_gettimeofday(&tv, &tz);  // invoke real libc_func
	
	if (mode) {   // replay
		//puts("replay gettimeofday");

		struct timeval tv_log;
		struct timezone tz_log;	
		int32 ret_log;
		
		char *libc_info_retval = query_libcfunc(GETTIMEOFDAY, pid);
		puts(libc_info_retval);

#if defined IA32
		sscanf(libc_info_retval, "%d, %d, %d, %d, %d", ret, &(tv_log.tv_sec), &(tv_log.tv_usec), &(tz_log.tz_minuteswest), &(tz_log.tz_dsttime));
#elif defined IA64
		sscanf(libc_info_retval, "%d, %lld, %lld, %d, %d", ret, &(tv_log.tv_sec), &(tv_log.tv_usec), &(tz_log.tz_minuteswest), &(tz_log.tz_dsttime));
#endif
		tp->tv_sec = tv.tv_sec;
		tp->tv_usec = tv.tv_usec;
		tzp->tz_minuteswest = tz.tz_minuteswest;
		tzp->tz_dsttime = tz.tz_dsttime;		
		
		/*printf("tv_sec: %d, tv_usec: %d, tz_minuteswest: %d, tz_dsttime: %d\n", tp->tv_sec, tp->tv_usec, tzp->tz_minuteswest, tzp->tz_dsttime);
		printf("replay result: tv_sec: %d, tv_usec: %d, tz_minuteswest: %d, tz_dsttime: %d\n", tv.tv_sec, tv.tv_usec, tz.tz_minuteswest, tz.tz_dsttime);
		*/

	} else {    // record
		puts("record gettimeofday");
#if defined IA32
		sprintf(retval, "%d, %d, %d, %d, %d", ret, (ret == -1) ? -1: tv.tv_sec, (ret == -1) ? -1: tv.tv_usec, (ret == -1) ? -1: tz.tz_minuteswest, (ret == -1) ? -1: tz.tz_dsttime);
#elif defined IA64
		sprintf(retval, "%d, %lld, %lld, %d, %d", ret, (ret == -1) ? -1: tv.tv_sec, (ret == -1) ? -1: tv.tv_usec, (ret == -1) ? -1: tz.tz_minuteswest, (ret == -1) ? -1: tz.tz_dsttime);
#endif
		libc_e *libc_info_w = (libc_e *)malloc(sizeof(libc_e) + strlen(retval) + 1);
		//strcpy((char *)(libc_info_w + 1), retval);
		strcpy(libc_info_w->retval, retval);

		libc_info_w->func_id = GETTIMEOFDAY;
		libc_info_w->pid = pid;
		libc_info_w->arg_num = 4; // except retval;

		write_libcfunc_log(libc_info_w);
		
		free(libc_info_w);
		
		if (ret != -1) {
			tp->tv_sec = tv.tv_sec;
			tp->tv_usec = tv.tv_usec;
			tzp->tz_minuteswest = tz.tz_minuteswest;
			tzp->tz_dsttime = tz.tz_dsttime;			
		}
		free(retval);
	}

	return ret;


}


