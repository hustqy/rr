#include "wrapper_io.h"

Fd_transfer_data recv_file(int sock) {
	struct msghdr msgh;
    struct iovec iov;
    int recv_fd;
	Fd_transfer_data recv_fd_data;
    ssize_t ret;

    union {
        struct cmsghdr cmh;
        char   control[CMSG_SPACE(sizeof(int))];                        
    } control_un;
    struct cmsghdr *cmhp;

	control_un.cmh.cmsg_len = CMSG_LEN(sizeof(int));
    control_un.cmh.cmsg_level = SOL_SOCKET;
    control_un.cmh.cmsg_type = SCM_RIGHTS;

    msgh.msg_control = control_un.control;
    msgh.msg_controllen = sizeof(control_un.control);

    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    iov.iov_base = &recv_fd_data;
    iov.iov_len = sizeof(Fd_transfer_data);

    msgh.msg_name = NULL;              
    msgh.msg_namelen = 0;

    ret = recvmsg(sock, &msgh, 0);
	if (ret == -1) {
		fprintf(stderr, "[%d] File = %s, Line = %d, Func = %s ", getpid(), __FILE__, __LINE__, __FUNCTION__);
		perror("recvmsg failed!");
	}
	
	cmhp = CMSG_FIRSTHDR(&msgh);
    if (cmhp == NULL || cmhp->cmsg_len != CMSG_LEN(sizeof(int)))
        perror("bad cmsg header / message length");
    if (cmhp->cmsg_level != SOL_SOCKET)
        perror("cmsg_level != SOL_SOCKET");
    if (cmhp->cmsg_type != SCM_RIGHTS)
        perror("cmsg_type != SCM_RIGHTS");

    recv_fd = *((int *) CMSG_DATA(cmhp));
	
	// fd_map: <real_fd, recv_fd>, real_fd -> data	
	recv_fd_data.recv_fd = recv_fd;
	return recv_fd_data;
}


void  send_file(int sock, int send_fd, int timestamp) {
	struct msghdr msgh;
    struct iovec iov;
	Fd_transfer_data send_data;

	// prepare Fd_transfer_data information
	send_data.real_fd= send_fd;
	send_data.timestamp = timestamp;
	send_data.recv_fd = 0;

    union {
        struct cmsghdr cmh;
        char   control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr *cmhp;

    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    iov.iov_base = &send_data;
    iov.iov_len = sizeof(Fd_transfer_data);

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;

    msgh.msg_control = control_un.control;
    msgh.msg_controllen = sizeof(control_un.control);

//    fprintf(stderr, "[IO PROCESS][%d] Sending fd %d\n", getpid(), send_fd);

    cmhp = CMSG_FIRSTHDR(&msgh);
    cmhp->cmsg_len = CMSG_LEN(sizeof(int));
    cmhp->cmsg_level = SOL_SOCKET;
    cmhp->cmsg_type = SCM_RIGHTS;
    *((int *) CMSG_DATA(cmhp)) = send_fd;

//	printf("[IO PROCESS][%d] <real_fd: %d, timestamp: %d>\n", getpid(), send_data.real_fd, send_data.timestamp);
	int ret = sendmsg(sock, &msgh, 0);
	if (ret == -1) {
		fprintf(stderr, "[IO PROCESS][%d] File = %s, Line = %d, Func = %s ",  getpid(), __FILE__, __LINE__, __FUNCTION__);
		perror("sendmsg failed!");
	}
}

