#ifndef _SMMAN_H
#define _SMMAN_H

#include "smsys.h"


int     sminit(void);

extern void    *smmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

extern int     smunmap(void *addr, size_t length);

extern void    *ssbrk(int increment);

extern void    *smremap(void *old_address, size_t old_size, size_t new_size, int flags);


#endif