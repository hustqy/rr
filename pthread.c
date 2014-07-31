#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "global.h"
#include "protocal.h"

int protected = 0;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg)
{
        pid_t pid;

        if (!protected)
        {
                protect_memory_init();
                protected = 1;
        }

        pid = fork();
        assert (pid >= 0);
        if ( pid == 0)
        {
				protect_memory();
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
