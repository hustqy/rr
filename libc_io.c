
#include "wrapper_io.h"

///// for client process ///////
int client_socket = 0;
pid_t client_pid;
Fd_Pinfo realfd_pmap[FILE_MAX];
///// for client process ///////

///// for IO process ///////
spinlock_t *fdmap_lock;      //mem shared
Fd_Sinfo *realfd_smap;      //mem shared

int server_socket;
pid_t IO_pid;
///// for IO process ///////

#ifdef TEST
spinlock_t *log_lock;  // used to log file
spinlock_t * read_log_lock; // used to read log file
int log_fd;  // record each io operation wrapper function invocation
int read_log_fd; // record the read operaiton buf
int args_fd;
#endif

int io_init_valid;  // used to init smap

// ptrpare shared area for IO fd_smap record and multiple client processess
void FD_smap_init(int size) {
	realfd_smap = (Fd_Sinfo *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0); 
	assert (realfd_smap != MAP_FAILED);
	
	fdmap_lock = (spinlock_t *)((ADDR)realfd_smap + sizeof(Fd_Sinfo) * FILE_MAX);
	//	printf("realfd_smap addr is 0x%x, sizeof(Fd_Sinfo) * \
	//		FILE_MAX is 0x%x, fdmap_lock addr is 0x%x\n", (unsigned int)realfd_smap,\
	//		sizeof(Fd_Sinfo) * FILE_MAX, (unsigned int)fdmap_lock);
		
	*fdmap_lock = SPIN_LOCK_UNLOCKED;
#ifdef TEST
	log_lock = fdmap_lock + 1;
	*log_lock = SPIN_LOCK_UNLOCKED;

	read_log_lock = log_lock + 1;
	*read_log_lock = SPIN_LOCK_UNLOCKED;
	
#endif
}

// this function must be invoked before creatting IO process and other child processess
void IO_init() {	
	int size = (PAGE_CEIL(sizeof(Fd_Sinfo) * FILE_MAX) + 1) * PAGE_SIZE;
	if (!io_init_valid) {
		FD_smap_init(size);
		io_init_valid = 1;
	}
}

#ifdef TEST
// just for debug
void IO_init_debug(char *log_fname, char *read_fname, char *arg_fname) {
	char *log_file = (char *)malloc(strlen(log_fname));
	strcpy(log_file, log_fname);

	//////////////////////////////////////////////////////////////////////
	// if log file exists, then delete it 
	if(access(log_file, F_OK) != -1 ) {
	    if (unlink(log_file) == -1)
			perror("unlink failed!");
	} 
	
	// log file record each io operation argument
	log_fd = _libc_open(log_file, O_CREAT | O_RDWR | O_APPEND, 00777);
	if (log_fd == -1)
		perror("create log file failed!");
	//////////////////////////////////////////////////////////////////////
	

	//////////////////////////////////////////////////////////////////////
	if(access(read_fname, F_OK) != -1 ) {
	    if (unlink(read_fname) == -1)
			perror("unlink failed!");
	} 
	
	// read log file record for READ  operation 
	read_log_fd = _libc_open(read_fname, O_CREAT | O_RDWR, 00777);
	if (read_log_fd == -1)
		perror("create read buf log file failed!");
	//////////////////////////////////////////////////////////////////////


	// record argument for each io operation by printting the screen
	//////////////////////////////////////////////////////////////////////
	if(access(arg_fname, F_OK) != -1 ) {
	    if (unlink(arg_fname) == -1)
			perror("unlink failed!");
	} 
	
	// log file record each io operation argument
	args_fd = _libc_open(arg_fname, O_CREAT | O_RDWR | O_APPEND, 00777);
	if (args_fd == -1)
		perror("create print argument log file failed!");
	//////////////////////////////////////////////////////////////////////
	
	int size = (PAGE_CEIL(sizeof(Fd_Sinfo) * FILE_MAX) + 1) * PAGE_SIZE;
	if (!io_init_valid) {
		FD_smap_init(size);
		io_init_valid = 1;
	}
	free(log_file);
}
#endif

// client process build socket connection
void do_connect_socket() {
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path));
	client_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	int ret = connect(client_socket, (struct sockaddr *)&addr, sizeof(struct sockaddr_un));
	if (ret == -1) {
		fprintf(stderr, "\n !!! ERROR !!! [%d] File = %s, Line = %d, Func = %s ", client_pid, __FILE__, __LINE__, __FUNCTION__);
		perror("connect failed!");
	}
}

// IO process listenning whether there is client request connection
void do_listen_socket() {
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(struct sockaddr_un));
	server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	addr.sun_family = AF_UNIX;
	if ((remove(SOCK_PATH) == -1) && errno != ENOENT) {
		fprintf(stderr, "\n !!! ERROR !!! [IO PROCESS][%d] File = %s, Line = %d, Func = %s ", IO_pid,  __FILE__, __LINE__, __FUNCTION__);
		perror("remove failed!");
	}
	strncpy(addr.sun_path, SOCK_PATH, strlen(SOCK_PATH));
	if (bind(server_socket, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
		fprintf(stderr, "\n !!! ERROR !!! [IO PROCESS][%d] File = %s, Line = %d, Func = %s ", IO_pid,  __FILE__, __LINE__, __FUNCTION__);
		perror("bind failed!");
	}
	listen(server_socket, BACK_LOG);
	printf("\t\033[5m\033[47;35m[IO PROCESS][%d] listenning...\033[0m\n", IO_pid);
}


int wrapper_open(const char *fname, int flag, ...) {
	int cfd_tmp, ret_send, ret_recv;
	int ret_val;  // open function return value
	R_info *request_data;
	Fd_transfer_data recv_fd_data;  // fd transfer information
	Op_transfer_data recv_op_data;  // operation transfer information

	// get the mode arg
	va_list argv;     
	va_start(argv, flag); 

	mode_t mode = va_arg(argv, mode_t);

	cfd_tmp = client_socket;

	// prepare request data
    request_data = malloc(sizeof(R_info));
    request_data->type = OPEN;
	request_data->client_pid = client_pid;
    strcpy(request_data->un.file_info.fname, fname);
    request_data->un.file_info.flag = flag;
    request_data->un.file_info.mode = mode;

	// send OPEN request to IO process 
	ret_send= send(cfd_tmp, request_data, sizeof(R_info), 0);
	if (ret_send == -1) {
		fprintf(stderr, "\n !!! ERROR !!! [%d] File = %s, Line = %d, Func = %s ", client_pid, __FILE__, __LINE__, __FUNCTION__);
		perror("send failed!");
	}
	free(request_data);

	ret_recv = recv(cfd_tmp, &recv_op_data, sizeof(Op_transfer_data), 0);
	if (ret_recv == -1) {
		fprintf(stderr, "\n !!! ERROR !!! [%d] File = %s, Line = %d, Func = %s ", client_pid, __FILE__, __LINE__, __FUNCTION__);
		perror("send failed!");
	}

	// IO process handle OPEN request successfully, and client process should recv the passed fd
	if (recv_op_data.op_success == SUCCESS) {
#ifdef DEBUG
		printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 1: \n\t\t\t\t\t\t\tOPEN_op sucess and need to recv fd!\033[0m\n", client_pid, __FUNCTION__);
#endif
		// recv response data from IO process
		recv_fd_data = recv_file(cfd_tmp);
		printf("[%d] in func: \" %s \" <real_fd : %d, recv_fd : %d, timestamp : %d> \n", client_pid, __FUNCTION__, recv_fd_data.real_fd, recv_fd_data.recv_fd, recv_fd_data.timestamp);

		// record corresponding real_fd info in process private fd_table
		realfd_pmap[recv_fd_data.real_fd].recv_fd = recv_fd_data.recv_fd;
		realfd_pmap[recv_fd_data.real_fd].p_timestamp = recv_fd_data.timestamp;
		ret_val = recv_fd_data.real_fd;
	}
	// IO process fail to handle OPEN request
	else {
		
#ifdef DEBUG
		printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 2: \n\t\t\t\t\t\t\tOPEN_op failure and return error!\033[0m\n", client_pid, __FUNCTION__);
#endif
		errno = recv_op_data.errno_num;
		ret_val = recv_op_data.ret_val;
	}	
	return ret_val;
}

int wrapper_close(int fd) {
	// the arg "fd" is real_fd

	int ret_val, cfd_tmp, ret_send, ret_close,ret_recv, s_timestamp, old_errno;
	R_info *request_data;
	BOOL fd_valid;
	Op_transfer_data recv_op_data;
#ifdef DEBUG
	int debug_flag = 0;
#endif

	cfd_tmp = client_socket;
	old_errno = errno;
	
	// temporarily store the fd's shared-timestamp 
	spin_lock(fdmap_lock);	
	
	s_timestamp = realfd_smap[fd].s_timestamp;	
	fd_valid = realfd_smap[fd].fd_valid;
	
	spin_unlock(fdmap_lock);

	if (fd_valid == true) {
		// prepare request data
		request_data = malloc(sizeof(R_info));
		request_data->un.obj_fd = fd;
		request_data->type = CLOSE;
		request_data->client_pid = client_pid;

		// send CLOSE request
		ret_send = send(cfd_tmp, request_data, sizeof(R_info), 0);
		if (ret_send == -1) {
			fprintf(stderr, "\n !!! ERROR !!! [%d] File = %s, Line = %d, Func = %s ", client_pid, __FILE__, __LINE__, __FUNCTION__);
			perror("send failed!");
			abort();
		}
		free(request_data);
		
		// recv Op_transfer_data info for CLOSE request from IO process
		ret_recv = recv(cfd_tmp, &recv_op_data, sizeof(Op_transfer_data), 0);
		if (ret_recv == -1) {
			fprintf(stderr, "\n !!! ERROR !!! [%d] File = %s, Line = %d, Func = %s ", client_pid, __FILE__, __LINE__, __FUNCTION__);
			perror("recv failed!");
			abort();
		}
		
		// current process did hold the fd
		int p_timestamp;
		if (realfd_pmap[fd].recv_fd != 0) {

			// temporarily store the fd's private-timestamp 
			p_timestamp = realfd_pmap[fd].p_timestamp;
			
			ret_close = _libc_close(realfd_pmap[fd].recv_fd);
			realfd_pmap[fd].recv_fd = 0;
			realfd_pmap[fd].p_timestamp = 0;

			// the process hold the newest fd
			if (p_timestamp == s_timestamp) {
#ifdef DEBUG			
				printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 1: \n\t\t\t\t\t\t\t CLOSE_fd is valid and hold the newest fd!\033[0m\n", client_pid, __FUNCTION__);
#endif			
				ret_val = ret_close;
				return ret_val;
			}
			// the process hold the older fd
			else if (p_timestamp != s_timestamp) {
				
#ifdef DEBUG
				debug_flag = 1;
				printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 2_1: \n\t\t\t\t\t\t\t CLOSE_fd is valid and hold the older fd!\033[0m\n", client_pid, __FUNCTION__);
#endif
				if (ret_close == -1) {
					fprintf(stderr, "\n !!! ERROR !!! [%d] File = %s, Line = %d, Func = %s ", client_pid, __FILE__, __LINE__, __FUNCTION__);
					perror("<<close>> failed within older fd!");
				}				
			}
		}
		// for the case both "hold the old fd" and "didn't hold the fd" need to modify errno and retval 
		// according to recv_data from IO process
#ifdef DEBUG
		if (debug_flag)
			printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 2_2: \n\t\t\t\t\t\t\t CLOSE_fd is valid and hold the older fd!\033[0m\n", client_pid, __FUNCTION__);
		else
			printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 3: \n\t\t\t\t\t\t\t CLOSE_fd is valid and didn't hold the fd!\033[0m\n", client_pid, __FUNCTION__);
#endif
		errno = (recv_op_data.ret_val == -1) ? recv_op_data.errno_num : old_errno;
		ret_val = recv_op_data.ret_val;
	}
	else {
#ifdef DEBUG
		printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 4: \n\t\t\t\t\t\t\t CLOSE_fd is invalid and return error !\033[0m\n", client_pid, __FUNCTION__);
#endif
		errno = EBADF;
		ret_val = -1;
	}
	return ret_val;
}

ssize_t wrapper_write(int fildes, const void *buf, size_t nbyte) {
	int cfd_tmp, s_timestamp, ret_send;
	ssize_t ret_val;
	BOOL fd_valid;  // judge the obj_fd is valid or invalid in IO process
	R_info *request_data;
	Fd_transfer_data recv_fd_data;
	Op_transfer_data recv_op_data;
#ifdef DEBUG
	int debug_flag = 0;
#endif

#ifdef TEST
	// record in log
	IO_arg io_arg;
	io_arg.type = WRITE;
	io_arg.pid = client_pid;
	io_arg.un_arg.write_args.obj_fd = fildes;
//	io_arg.un_arg.write_args.buf = malloc(nbyte);
//	memcpy(io_arg.un_arg.write_args.buf, buf, nbyte);
	io_arg.un_arg.write_args.nbyte = nbyte;

	char arg_buf[256];
	sprintf(arg_buf, "[[ARGS]] [%d] WRITE: fd is %d, buf is %s, size is %d ...\n", client_pid, fildes, (char *)buf, nbyte);
	
	spin_lock(log_lock);
	_libc_write(log_fd, &io_arg, sizeof(int) * 2 + sizeof(size_t) + sizeof(pid_t)); // delete the size of "void *"
	_libc_write(log_fd, buf, nbyte);
	_libc_write(args_fd, arg_buf, strlen(arg_buf));
	spin_unlock(log_lock);
	
#endif

	cfd_tmp = client_socket;
	
	spin_lock(fdmap_lock);
	
	s_timestamp = realfd_smap[fildes].s_timestamp;
	fd_valid = realfd_smap[fildes].fd_valid;
	
	spin_unlock(fdmap_lock);

	// the fildes is a valid fd in IO process space
	if (fd_valid == true) {
		if (realfd_pmap[fildes].recv_fd != 0) {
			// fildes exits in client process, and it is the newest fd, so using it directly
			if (realfd_pmap[fildes].p_timestamp == s_timestamp) {
#ifdef DEBUG
				printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 1: \n\t\t\t\t\t\t\tUSE_fd is valid and the client process hold the newest fd !\033[0m\n", client_pid, __FUNCTION__);
#endif
				ret_val = _libc_write(realfd_pmap[fildes].recv_fd, buf, nbyte);

				return ret_val;
			}
			// fildes exits in client process, but it is not the newest fd which in IO process, so need to
			// close the older fd first, and then reques USE to IO process
			else if (realfd_pmap[fildes].p_timestamp != s_timestamp) {
#ifdef DEBUG
				debug_flag = 1;
				printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 2_1: \n\t\t\t\t\t\t\tUSE_fd is valid and  the client process hold the older fd , so close the older fd!\033[0m\n", client_pid, __FUNCTION__);
#endif
				_libc_close(realfd_pmap[fildes].recv_fd);
			}
		}	
		// send USE request to IO process
		request_data = malloc(sizeof(R_info));
		request_data->type = USE;
		request_data->client_pid = client_pid;
		request_data->un.obj_fd = fildes;
		
		// prepare sending data
		ret_send = send(cfd_tmp, request_data, sizeof(R_info), 0);
		
		if (ret_send == -1) {
			fprintf(stderr, "\n !!! ERROR !!! [%d] File = %s, Line = %d, Func = %s ", client_pid, __FILE__, __LINE__, __FUNCTION__);
			perror("send failed!");
		}
		free(request_data);
			
		// recv fd data from IO process
		recv_fd_data = recv_file(cfd_tmp);
#ifdef DEBUG
		if (debug_flag)
			printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 2_2: \n\t\t\t\t\t\t\tUSE_fd is valid and  the client process hold the older fd , so need to recv the newest fd from IO process!\033[0m\n", client_pid, __FUNCTION__);
		else
			printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 3: \n\t\t\t\t\t\t\tUSE_fd is valid and  the client process doesn't hold the fd , so need to recv the newest fd from IO process!\033[0m\n", client_pid, __FUNCTION__);
#endif
		printf("[%d] in func: \" %s \" <real_fd : %d, recv_fd : %d, timestamp : %d> \n", client_pid, __FUNCTION__, recv_fd_data.real_fd, recv_fd_data.recv_fd, recv_fd_data.timestamp);
		
		// record corresponding real_fd info in process private fd_table
		realfd_pmap[recv_fd_data.real_fd].recv_fd = recv_fd_data.recv_fd;
		realfd_pmap[recv_fd_data.real_fd].p_timestamp = recv_fd_data.timestamp; 
		
		// do real write operation
		ret_val = _libc_write(recv_fd_data.recv_fd, buf, nbyte);
	}
	// the fildes is not valid fd in IO process space, i.e. it's not ready open for using
	else {
#ifdef DEBUG
		printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 4: \n\t\t\t\t\t\t\tUSE_fd is invalid and return error !\033[0m\n", client_pid, __FUNCTION__);
#endif
		errno = EBADF;
		ret_val = -1;
	}
	return ret_val;
}

off_t wrapper_lseek(int fildes, off_t offset, int whence) {
	int cfd_tmp, s_timestamp, ret_send, ret_val;
	BOOL fd_valid;   // judge the obj_fd is valid or invalid in IO process
	R_info *request_data;
	Fd_transfer_data recv_fd_data;
	Op_transfer_data recv_op_data;
#ifdef DEBUG
	int debug_flag = 0;
#endif

#ifdef TEST
	// record in log
	IO_arg io_arg;
	io_arg.type = LSEEK;
	io_arg.pid = client_pid;
	io_arg.un_arg.lseek_args.fildes = fildes;
	io_arg.un_arg.lseek_args.offset = offset;
	io_arg.un_arg.lseek_args.whence = whence;

	char arg_buf[256];
	sprintf(arg_buf, "[[ARGS]] [%d] LSEEK: fd is %d, offset is %d, whence is %d ...\n", client_pid, fildes, offset, whence);
	
	spin_lock(log_lock);
	_libc_write(log_fd, &io_arg, sizeof(IO_arg));
	_libc_write(args_fd, arg_buf, strlen(arg_buf));
	spin_unlock(log_lock);	
#endif

	cfd_tmp = client_socket;
	
	spin_lock(fdmap_lock);
	
	s_timestamp = realfd_smap[fildes].s_timestamp;
	fd_valid = realfd_smap[fildes].fd_valid;
	
	spin_unlock(fdmap_lock);

	// the fildes is a valid fd in IO process space
	if (fd_valid == true) {
		if (realfd_pmap[fildes].recv_fd != 0) {
			// fildes exits in client process, and it is the newest fd, so using it directly
			if (realfd_pmap[fildes].p_timestamp == s_timestamp) {
#ifdef DEBUG
				printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 1: \n\t\t\t\t\t\t\tUSE_fd is valid and the client process hold the newest fd !\033[0m\n", client_pid, __FUNCTION__);
#endif
				ret_val = _libc_lseek(realfd_pmap[fildes].recv_fd, offset, whence);

				return ret_val;
			}
			// fildes exits in client process, but it is not the newest fd which in IO process, so need to
			// close the older fd first, and then reques USE to IO process
			else if (realfd_pmap[fildes].p_timestamp != s_timestamp) {
#ifdef DEBUG
				debug_flag = 1;
				printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 2_1: \n\t\t\t\t\t\t\tUSE_fd is valid and  the client process hold the older fd , so close the older fd!\033[0m\n", client_pid, __FUNCTION__);
#endif
				_libc_close(realfd_pmap[fildes].recv_fd);
			}
		}	
		// send USE request to IO process
		request_data = malloc(sizeof(R_info));
		request_data->type = USE;
		request_data->client_pid = client_pid;
		request_data->un.obj_fd = fildes;
		
		// prepare sending data
		ret_send = send(cfd_tmp, request_data, sizeof(R_info), 0);
		
		if (ret_send == -1) {
			fprintf(stderr, "\n !!! ERROR !!! [%d] File = %s, Line = %d, Func = %s ", client_pid, __FILE__, __LINE__, __FUNCTION__);
			perror("send failed!");
		}
		free(request_data);
			
		// recv fd data from IO process
		recv_fd_data = recv_file(cfd_tmp);
#ifdef DEBUG
		if (debug_flag)
			printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 2_2: \n\t\t\t\t\t\t\tUSE_fd is valid and  the client process hold the older fd , so need to recv the newest fd from IO process!\033[0m\n", client_pid, __FUNCTION__);
		else
			printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 3: \n\t\t\t\t\t\t\tUSE_fd is valid and  the client process doesn't hold the fd , so need to recv the newest fd from IO process!\033[0m\n", client_pid, __FUNCTION__);
#endif
		printf("[%d] in func: \" %s \" <real_fd : %d, recv_fd : %d, timestamp : %d> \n", client_pid, __FUNCTION__, recv_fd_data.real_fd, recv_fd_data.recv_fd, recv_fd_data.timestamp);
		
		// record corresponding real_fd info in process private fd_table
		realfd_pmap[recv_fd_data.real_fd].recv_fd = recv_fd_data.recv_fd;
		realfd_pmap[recv_fd_data.real_fd].p_timestamp = recv_fd_data.timestamp; 
		
		// do real write operation
		ret_val = _libc_lseek(recv_fd_data.recv_fd, offset, whence);
	}
	// the fildes is not valid fd in IO process space, i.e. it's not ready open for using
	else {
#ifdef DEBUG
		printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 4: \n\t\t\t\t\t\t\tUSE_fd is invalid and return error !\033[0m\n", client_pid, __FUNCTION__);
#endif
		errno = EBADF;
		ret_val = -1;
	}
	return ret_val;

}

ssize_t wrapper_read(int fildes, void *buf, size_t nbyte) {
		int cfd_tmp, s_timestamp, ret_send;
		ssize_t ret_val;
		BOOL fd_valid;	// judge the obj_fd is valid or invalid in IO process
		R_info *request_data;
		Fd_transfer_data recv_fd_data;
		Op_transfer_data recv_op_data;
#ifdef DEBUG
		int debug_flag = 0;
#endif
	
#ifdef TEST
		// record in log
		IO_arg io_arg;
		io_arg.type = READ;
		io_arg.pid = client_pid;
		io_arg.un_arg.read_args.obj_fd = fildes;
		io_arg.un_arg.read_args.nbyte = nbyte;

		char arg_buf[256];
		sprintf(arg_buf, "[[ARGS]] [%d] READ: fd is %d, size is %d ...\n", client_pid, fildes, nbyte);
		
		
		spin_lock(log_lock);
		_libc_write(log_fd, &io_arg, sizeof(IO_arg)); 
		_libc_write(args_fd, arg_buf, strlen(arg_buf));
		spin_unlock(log_lock);		
#endif
		
		cfd_tmp = client_socket;
		
		spin_lock(fdmap_lock);
		
		s_timestamp = realfd_smap[fildes].s_timestamp;
		fd_valid = realfd_smap[fildes].fd_valid;
		
		spin_unlock(fdmap_lock);
	
		// the fildes is a valid fd in IO process space
		if (fd_valid == true) {
			if (realfd_pmap[fildes].recv_fd != 0) {
				// fildes exits in client process, and it is the newest fd, so using it directly
				if (realfd_pmap[fildes].p_timestamp == s_timestamp) {
#ifdef DEBUG
					printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 1: \n\t\t\t\t\t\t\tUSE_fd is valid and the client process hold the newest fd !\033[0m\n", client_pid, __FUNCTION__);
#endif
					ret_val = _libc_read(realfd_pmap[fildes].recv_fd, buf, nbyte);
#ifdef TEST
					if (ret_val != 0) {
						spin_lock(read_log_lock);
						_libc_write(read_log_fd, buf, nbyte);
						spin_unlock(read_log_lock);
					}
#endif
	
					return ret_val;
				}
				// fildes exits in client process, but it is not the newest fd which in IO process, so need to
				// close the older fd first, and then reques USE to IO process
				else if (realfd_pmap[fildes].p_timestamp != s_timestamp) {
#ifdef DEBUG
					debug_flag = 1;
					printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 2_1: \n\t\t\t\t\t\t\tUSE_fd is valid and  the client process hold the older fd , so close the older fd!\033[0m\n", client_pid, __FUNCTION__);
#endif
					_libc_close(realfd_pmap[fildes].recv_fd);
				}
			}	
			// send USE request to IO process
			request_data = malloc(sizeof(R_info));
			request_data->type = USE;
			request_data->client_pid = client_pid;
			request_data->un.obj_fd = fildes;
			
			// prepare sending data
			ret_send = send(cfd_tmp, request_data, sizeof(R_info), 0);
			
			if (ret_send == -1) {
				fprintf(stderr, "\n !!! ERROR !!! [%d] File = %s, Line = %d, Func = %s ", client_pid, __FILE__, __LINE__, __FUNCTION__);
				perror("send failed!");
			}
			free(request_data);
				
			// recv fd data from IO process
			recv_fd_data = recv_file(cfd_tmp);
#ifdef DEBUG
			if (debug_flag)
				printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 2_2: \n\t\t\t\t\t\t\tUSE_fd is valid and  the client process hold the older fd , so need to recv the newest fd from IO process!\033[0m\n", client_pid, __FUNCTION__);
			else
				printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 3: \n\t\t\t\t\t\t\tUSE_fd is valid and  the client process doesn't hold the fd , so need to recv the newest fd from IO process!\033[0m\n", client_pid, __FUNCTION__);
#endif
			printf("[%d] in func: \" %s \" <real_fd : %d, recv_fd : %d, timestamp : %d> \n", client_pid, __FUNCTION__, recv_fd_data.real_fd, recv_fd_data.recv_fd, recv_fd_data.timestamp);
			
			// record corresponding real_fd info in process private fd_table
			realfd_pmap[recv_fd_data.real_fd].recv_fd = recv_fd_data.recv_fd;
			realfd_pmap[recv_fd_data.real_fd].p_timestamp = recv_fd_data.timestamp; 
			
			// do real write operation
			ret_val = _libc_read(recv_fd_data.recv_fd, buf, nbyte);
#ifdef TEST
					  // maybe read operation will return 0 in case of no data in file
			if (ret_val > 0) {
				spin_lock(read_log_lock);
				_libc_write(read_log_fd, buf, nbyte);
				spin_unlock(read_log_lock);
			}
#endif

		}
		// the fildes is not valid fd in IO process space, i.e. it's not ready open for using
		else {
#ifdef DEBUG
			printf("\033[32m[[ DEBUGGING~~~ ]][%d] func: \" %s \" case 4: \n\t\t\t\t\t\t\tUSE_fd is invalid and return error !\033[0m\n", client_pid, __FUNCTION__);
#endif
			errno = EBADF;
			ret_val = -1;
		}
		return ret_val;


}
void do_open(char *fname, int flags, mode_t mode, int client_socket, int client_pid) {
	int ret_send, old_errno, send_fd;
	Op_transfer_data send_op_data;

#ifdef TEST
	// record in log
	IO_arg io_arg;
	io_arg.type = OPEN;
	io_arg.pid = client_pid;
	io_arg.un_arg.open_args.flag = flags;
	strcpy(io_arg.un_arg.open_args.fname, fname);
	io_arg.un_arg.open_args.mode = mode;

	spin_lock(log_lock);
	_libc_write(log_fd, &io_arg, sizeof(IO_arg));
	spin_unlock(log_lock);

#endif
	// step 1. open file
	send_fd = _libc_open(fname, flags, mode);

#ifdef TEST
		char arg_buf[256];
		sprintf(arg_buf, "[[ARGS]] [%d] OPEN : fname is %s, flag is %d, mode is o%o , fd is %d ...\n", client_pid, fname, flags, mode, send_fd);
		_libc_write(args_fd, arg_buf, strlen(arg_buf));	
#endif 	
	// step 2. prepare Op_transfer_data to send to client process
	send_op_data.op_success = (send_fd == -1) ? FAILURE : SUCCESS;
	send_op_data.errno_num = (send_fd == -1) ? errno : 0;
	send_op_data.ret_val = send_fd;

	// step 3. IO process send 	Op_transfer_data to client process
	ret_send = send(client_socket, &send_op_data, sizeof(Op_transfer_data), 0);
	if (ret_send == -1) {
		fprintf(stderr, "\n !!! ERROR !!! [IO PROCESS][%d] File = %s, Line = %d, Func = %s ", IO_pid,  __FILE__, __LINE__, __FUNCTION__);
		perror("send failed!");		
	}
	
	// step 4. if IO process handle OPEN request successfully, then send fd to the client process which request OPEN
	if (send_fd != -1) {		
		// mark the real_fd 's timestamp
		spin_lock(fdmap_lock);
		
		realfd_smap[send_fd].s_timestamp += 1;
		realfd_smap[send_fd].fd_valid = true;
#ifdef DEBUG
		strcpy(realfd_smap[send_fd].fname, fname);
#endif
		int s_timestamp = realfd_smap[send_fd].s_timestamp;
		
		spin_unlock(fdmap_lock);
		// send fd and fd's s_timestamp
		send_file(client_socket, send_fd, s_timestamp); 
		printf("\033[33m[IO PROCESS][%d] func \" %s \" send fd <real_fd: %d, timestamp: %d>\033[0m\n", IO_pid, __FUNCTION__, send_fd, s_timestamp);		
	}		
	return;
}

void do_use(int obj_fd, int client_socket) {
	assert(realfd_smap[obj_fd].s_timestamp != 0);
	assert(realfd_smap[obj_fd].fd_valid != false);

	int s_timestamp;
	Op_transfer_data send_op_data;
	
	spin_lock(fdmap_lock);	

	s_timestamp = realfd_smap[obj_fd].s_timestamp;
	
	spin_unlock(fdmap_lock);

	
	// IO process send file descriptor to client under socket
	send_file(client_socket, obj_fd, s_timestamp);	
	//	printf("\033[33m[IO PROCESS][%d] listenning...\n\033[0m", IO_pid);
	printf("\033[33m[IO PROCESS][%d] func \" %s \" send fd <real_fd: %d, timestamp: %d>\033[0m\n", IO_pid, __FUNCTION__, obj_fd, s_timestamp);
	
	return;
}

void do_close(int obj_fd, int client_socket, int client_pid) {
	
	assert(realfd_smap[obj_fd].s_timestamp != 0);
	assert(realfd_smap[obj_fd].fd_valid != false);

	int ret_val, ret_send; 
	Op_transfer_data send_op_data;

#ifdef TEST
	// record in log
	IO_arg io_arg;
	io_arg.type = CLOSE;
	io_arg.pid = client_pid;
	io_arg.un_arg.close_args = obj_fd;

	char arg_buf[256];
	sprintf(arg_buf, "[[ARGS]] [%d] CLOSE obj_fd is %d .... \n", client_pid, obj_fd);
	
	spin_lock(log_lock);
	_libc_write(log_fd, &io_arg, sizeof(IO_arg));
	_libc_write(args_fd, arg_buf, strlen(arg_buf));
	spin_unlock(log_lock);

#endif
	// IO process do real "close" operation
	printf("\033[33m[IO PROCESS][%d] func \" %s \" really do close operation! \033[0m\n", IO_pid, __FUNCTION__);
	ret_val = _libc_close(obj_fd);

	// IO process send Op_transfer_data to client process
	send_op_data.op_success = (ret_val == -1) ? FAILURE : SUCCESS;
	send_op_data.errno_num = (ret_val == -1) ? errno : 0;
	send_op_data.ret_val = ret_val;
	
	ret_send = send(client_socket, &send_op_data, sizeof(Op_transfer_data), 0);
	if (ret_send == -1) {
		fprintf(stderr, "\n !!! ERROR !!! [IO PROCESS][%d] File = %s, Line = %d, Func = %s  \n", IO_pid, __FILE__, __LINE__, __FUNCTION__);
		perror("send failed!");
	}
	
	spin_lock(fdmap_lock);
	realfd_smap[obj_fd].fd_valid = false;
#ifdef DEBUG
	strcpy(realfd_smap[obj_fd].fname, "");
#endif
	spin_unlock(fdmap_lock);
	return;
}

// IO process handle the different IO request
void do_serve(int client_socket, R_info *data) {
	int type = data->type;
	
	// the IO request process pid
	int client_pid = data->client_pid;

	switch(type) {		
		case OPEN:
				printf("\n\t\033[41;37m[IO PROCESS][%d] handle [[OPEN]] from client [%d]\t { <fname> : \"%s\", <flags> : 0x%x, <mode> : 0%o }\033[0m \n\n", IO_pid, client_pid, \
			data->un.file_info.fname, data->un.file_info.flag, data->un.file_info.mode);	
			
			// IO process handle OPEN request
			do_open(data->un.file_info.fname, data->un.file_info.flag, data->un.file_info.mode, client_socket, client_pid);
			break;
			
		case CLOSE:
			printf("\n\t\033[41;37m[IO PROCESS][%d] handle [[CLOSE]] from client [%d]\033[0m \n\n", IO_pid, client_pid);
			// IO process handle CLOSE request
			do_close(data->un.obj_fd, client_socket, client_pid);
			break;
			
		case USE:
			printf("\n\t\033[41;37m[IO PROCESS][%d] handle [[USE]] from client [%d]\033[0m \n\n", IO_pid, client_pid);
			// IO process handle USE request
			do_use(data->un.obj_fd, client_socket);
			break;
			
		default:
			// IO process handle NOT_IMPLEMENT request
			fprintf(stderr, "\n !!! ERROR !!!  [IO PROCESS][%d] handle [[%s]] from client [%d] \n\n", IO_pid, NOT_IMPLEMENT, client_pid);
			break;	
	}
	return;
}

void io_process() {
	fd_set r_sockset;
	int sock_max = 0;
	int connect_num = 0;
	int fd_A[BACK_LOG] = {0};
	sock_max = server_socket;

	struct sockaddr_un client_addr;
	int client_sock;
	socklen_t size;
	size = sizeof(struct sockaddr_un);
	while(1) {
		FD_ZERO(&r_sockset);
		FD_SET(server_socket, &r_sockset);
		int i;		
		for(i = 0; i < connect_num; i++) {
			if(fd_A[i] != 0)
				FD_SET(fd_A[i], &r_sockset);
		}
		int ret = select(sock_max + 1, &r_sockset, NULL, NULL, NULL);
		
		if (ret == -1)  {
			fprintf(stderr, "\n !!! ERROR !!! [IO PROCESS][%d] File = %s, Line = %d, Func = %s ", IO_pid,  __FILE__, __LINE__, __FUNCTION__);
			perror("select failed!");
		}

		if FD_ISSET(server_socket, &r_sockset) {
			client_sock = accept(server_socket, (struct sockaddr *)&client_addr, &size);
			if (client_sock == -1) {
				fprintf(stderr, "\n !!! ERROR !!! [IO PROCESS][%d] File = %s, Line = %d, Func = %s ", IO_pid, __FILE__, __LINE__, __FUNCTION__);
				perror("accept failed!");
			}

			fd_A[connect_num++] = client_sock;
			if (client_sock > sock_max) {
				sock_max = client_sock;
			}
			continue;
		}
		// malloc space for recv data.
		R_info *request_data = (R_info *)malloc(sizeof(R_info));
		for (i = 0; i < connect_num; i++) {
			if FD_ISSET(fd_A[i], &r_sockset) {				
				int ret = recv(fd_A[i], request_data, sizeof(R_info), 0);
				
				// the client socket connection is closed 
				if (ret == 0) {
					FD_CLR(fd_A[i], &r_sockset);
					_libc_close(fd_A[i]);
					fd_A[i] = 0;
					continue;
				}
				else if(ret == -1) {
					fprintf(stderr, "\n !!! ERROR !!! [IO PROCESS][%d] File = %s, Line = %d, Func = %s ",  IO_pid, __FILE__, __LINE__, __FUNCTION__);
					perror("recv failed!");
				}
				else
					do_serve(fd_A[i], request_data);
			}	
		}
		free(request_data);
	}
}

