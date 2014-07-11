#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "global.h"
#include "protocal.h"

void _exit (int status)
{
	pid_t pid = getpid();

	fprintf (stderr, "[%d] exit...\n", pid);

	give_up_ownership (pid);

	_libc_exit (status);

	assert(0);
	_exit (status);
}
