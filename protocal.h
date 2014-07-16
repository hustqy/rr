#ifndef _PROTOCAL_H_

#define _PROTOCAL_H_

typedef enum { PUBLIC=0, SHARED_READ=1, OWNED_WRITE=2}page_state;

typedef enum { AC_READ=0, AC_WRITE=1 }ac_type;


extern void acquire_ownership(unsigned long page_start_addr, pid_t pid, ac_type type);

extern void give_up_ownership (pid_t pid);

#endif
