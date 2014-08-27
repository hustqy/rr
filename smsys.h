#ifndef _SMSYS_H
#define _SMSYS_H

#if defined(__STDC__) || defined (__cplusplus)
# include <stddef.h>
#else
# undef  size_t
# define size_t                 unsigned int
#endif

#include <sys/types.h>
#include "global.h"

#ifdef __x86_64__
    typedef unsigned long int   taddr; 
#elif __i386__
    typedef unsigned int        taddr;
#endif


#endif