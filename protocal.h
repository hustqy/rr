#ifndef _PROTOCAL_H_

#define _PROTOCAL_H_

extern void acquire_ownership (unsigned long page_start_addr, pid_t pid, int type);

extern void give_up_ownership (pid_t pid);

#endif
