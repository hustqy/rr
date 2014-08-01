#include <stdio.h>
#include <pthread.h>
#include<assert.h>
int x = 0;
int a[1024];
//int a[1024]= {12,12};
int *p;

void *child1(void *arg)
{
	x = 1;

	int i;
	for(i=0;i<1024;++i){
		a[i] = 10;
	}
	return NULL;
}

void *child2(void *arg)
{
	x = 2;
	int i;
	for(i=0;i<1024;++i){
		a[i] = 20;
	}
	return NULL;
}

int main (int argc, char **argv)
{

	p = &x+0x1000;

	printf("in test 0\n");
	pthread_t thread1, thread2;
	
	printf("in test 1\n");

	pthread_create(&thread1, NULL, child1, NULL);
	printf("in test 2 ------a child here\n");
	pthread_create(&thread2, NULL, child2, NULL);

	pthread_join(thread1, NULL);
	pthread_join(thread2, NULL);

	printf ("x = %d, a[1023] = %d, &a[1023] = %p\n", x,a[1023],&a[1023]);
	sleep(60);
	return 0;
}
