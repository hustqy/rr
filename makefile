CC=gcc
#CC=g++
CFLAGS=-g -fPIC
#CFLAGS=-g -mcmodel=large -fPIC
#LDFLAGS=-pthread -fPIC -shared -ldl 
LDFLAGS=-ldl -pthread -fPIC -shared
TESTFLAGS=-pthread

WRAPPER=btrecorder
LIB=libxxx.so
TEST=test

all:
	$(CC) test.c -c -o test.o
	$(CC) $(TESTFLAGS) test.o -o $(TEST)
	$(CC) $(CFLAGS) smman.c -c -o smman.o
	$(CC) $(CFLAGS) malloc.c -c -o malloc.o
	$(CC) $(CFLAGS) script.c -c -o script.o
	$(CC) $(CFLAGS) main.c -c -o main.o
	$(CC) $(CFLAGS) atomic.c -c -o atomic.o
	$(CC) $(CFLAGS) protocal.c -c -o protocal.o
	$(CC) $(CFLAGS) pthread.c -c -o pthread.o
	$(CC) $(CFLAGS) libc.c -c -o libc.o
	$(CC) $(CFLAGS) script.o -o $(WRAPPER)
#	$(CC) $(CFLAGS) $(LDFLAGS) main.o atomic.o protocal.o pthread.o libc.o -o $(LIB)
	$(CC) $(CFLAGS) main.o atomic.o protocal.o pthread.o libc.o malloc.o smman.o $(LDFLAGS) -o $(LIB)

clean:
	rm -rf *.o $(TEST) $(WRAPPER) $(LIB)
