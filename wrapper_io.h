#ifndef _WRAPPER_IO_H_
#define _WRAPPER_IO_H_

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdarg.h> 

#include "global.h"

#define IA64
#if 0
#define DEBUG
#define TEST
#endif
#define BUF_MAX 128
#define FILE_MAX 256  // the file descriptor table item num in IO process shared with any other client processess
#define BACK_LOG 256 // the maxum listernning number of IO process server process 
#define SPIN_LOCK_UNLOCKED 0

#define SOCK_PATH "fd_pass_yh"
#define NOT_IMPLEMENT "Not implemented!"

#define PAGE_SIZE getpagesize()
#define PAGE_MASK (~(PAGE_SIZE -1))
#define PAGE_CEIL(addr) (((addr)+PAGE_SIZE-1)&PAGE_MASK)

#define wrapper_open open
#define wrapper_close close
#define wrapper_lseek lseek
#define wrapper_read read
#define wrapper_write write

typedef int int32;
typedef long long int64;
typedef unsigned int uint32;

#ifdef IA32
	typedef int32 ADDR;
#elif defined IA64
	typedef int64 ADDR;
#endif

// IO request type
enum {
    OPEN = 1,
    CLOSE = 2,
    WRITE = 3,
    LSEEK = 4,
    READ = 5,
    USE = 6    
};
typedef enum {false, true} BOOL;

typedef struct {
    char fname[BUF_MAX];
    int flag;
    mode_t mode;
}File_info;

typedef struct {
    int type;  // request type
	pid_t client_pid;  // requese client process pid
    union {                  // for open request
        File_info file_info;
        int obj_fd;
    }un;
}R_info;

typedef struct {
	int recv_fd;
	int p_timestamp;  // for update real_fd
}Fd_Pinfo;  // private for each client process

#ifdef DEBUG

	typedef struct {
		int s_timestamp;
		BOOL fd_valid;  // if current real_fd is closed, then fd_valid is false, otherwise, as long as the file is opend, the flag is true 
		char fname[64]; // for debug
	}Fd_Sinfo; // shared between IO process and each client process
#else
	typedef struct {
		int s_timestamp;
		BOOL fd_valid;  // if current real_fd is closed, then fd_valid is false, otherwise, as long as the file is opend, the flag is true 
	}Fd_Sinfo; // shared between IO process and each client process

#endif

// fd information transfer from IO process to client process
typedef struct {
	int real_fd;  // real fd IO process send
	int timestamp;  // real fd 's timestamp IO process send
	int recv_fd;  // the fd the client process recieved
}Fd_transfer_data;

//  indicate the operation status of "open" or "close" in IO process
enum {
	SUCCESS = 1,
	FAILURE = 2
};

// operation information transfer from IO process to client process
typedef struct {
	int op_success; // indicate the operation is successful or not: SUCCESS or FAILURE
	int ret_val;  // for CLOSE or other request, IO process 's retval after doing the operation 
	int errno_num; // for CLOSE or other request, IO process 's errno after doing the operation 
}Op_transfer_data;

#ifdef TEST
	typedef File_info Open_t;
	typedef int Close_t;

	typedef struct {
		int obj_fd;
		size_t nbyte;
		void *buf;
	}Write_t;

	typedef struct {
		int obj_fd;
		size_t nbyte;
	} Read_t;

	typedef struct {
		int fildes; 
		off_t offset; 
		int whence;
	}Lseek_t;
	
	// for debug wrapper_open, wrapper_close, wrapper_write, record the argument of them invocation
	typedef struct {
		int type;
		pid_t pid;
		union {
		Open_t open_args;
		Close_t close_args;
		Write_t write_args;
		Read_t read_args;
		Lseek_t lseek_args;
		}un_arg;
	}IO_arg;
#endif

extern int errno;
extern int log_fd; // record IO operation in log file
extern int read_log_fd; // record the read operaiton buf
extern int args_fd;  // record all the operation args

// for client process ///////
extern int client_socket;
extern pid_t client_pid;
extern Fd_Pinfo realfd_pmap[FILE_MAX];
// for client process ///////

// for IO process ///////
extern spinlock_t *fdmap_lock;  // mem shared
extern Fd_Sinfo *realfd_smap;   // mem shared
extern int server_socket;
extern pid_t IO_pid;
// for IO process ///////

extern Fd_transfer_data recv_file(int sock);
extern void send_file(int sock, int send_fd, int timestamp);
extern void FD_smap_init(int size);
#ifdef TEST
	extern void IO_init_debug(char *log_fname, char *read_fname, char *arg_fname); // just for debugging
#endif
extern void IO_init();
extern void do_connect_socket();
extern void do_listen_socket();
extern int wrapper_open(const char *fname, int flag, ...);
extern int wrapper_close(int fd);
extern ssize_t wrapper_write(int fildes, const void *buf, size_t nbyte);
extern off_t wrapper_lseek(int fildes, off_t offset, int whence);
extern ssize_t wrapper_read(int fildes, void *buf, size_t nbyte);
extern void do_open(char *fname, int flags, mode_t mode, int client_socket, int client_pid);
extern void do_use(int obj_fd, int client_socket);
extern void do_close(int obj_fd, int client_socket, int client_pid);
extern void do_serve(int client_socket, R_info *data);
extern void io_process();

#endif






