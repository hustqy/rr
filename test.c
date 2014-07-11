#include <stdio.h>
#include <pthread.h>

int x = 0;

void *child1(void *arg)
{
	x = 1;
	malloc(NULL);
	return NULL;
}

void *child2(void *arg)
{
	x = 2;
	return NULL;
}

int main (int argc, char **argv)
{
	pthread_t thread1, thread2;

	pthread_create(&thread1, NULL, child1, NULL);
	pthread_create(&thread2, NULL, child2, NULL);

	pthread_join(thread1, NULL);
	pthread_join(thread2, NULL);

	printf ("x = %d\n", x);
	return 0;
}
