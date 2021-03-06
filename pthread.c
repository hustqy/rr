#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "global.h"
#include "protocal.h"
#include "wrapper_io.h"

int protected = 0;
int fork_io = 0;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg)
{

	printf("begin of pthread_creat \n");
        
		pid_t pid;

        if (!protected)
        {
			printf("before first mem protect in pot\n");
			//2014.7.31:problem here
                protect_memory_init();
			printf("after first mem protect in pot\n");	
                protected = 1;
        }

		if (!fork_io) {
			IO_init();   // prepare for IO_wrapper functions

			pid_t io_pid;
			io_pid = fork();
			if (io_pid == 0) {
				IO_pid = getpid();
				do_listen_socket();
				io_process();
				_libc_exit(88);
			}
			sleep(2);   // wait until io has been ready to handle connection request
			fork_io = 1;
		}
		
	printf("going to fork now\n");
        pid = fork();
        assert (pid >= 0);
	
	printf("fork success\n");

        if ( pid == 0)
        {
				protect_memory();
	printf("before child execv\n");
                start_routine(arg);
                _exit (0);
        }
        else if (pid > 0)
        {
                *thread = pid;
                fprintf (stderr, "create child: %d\n", pid);
        }
}

int pthread_join(pthread_t thread, void **retval)
{
        pid_t pid = *(pid_t *)&thread;

		give_up_ownership (getpid());

        waitpid (pid, NULL, __WALL);
}
