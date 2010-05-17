#ifndef _ALLOCATOR_IOCTL_H
#define _ALLOCATOR_IOCTL_H

#include <linux/ioctl.h>

#include "mem_user.h" //include the user space specific stuffs

#define ALLOCATOR_TYPE 'k'
#define _IOC_ALLOCATOR_HARDRESET _IO(ALLOCATOR_TYPE,1)
#define _IOC_ALLOCATOR_GETPAGES  _IOR(ALLOCATOR_TYPE,2,struct mem_user)
#define _IOC_ALLOCATOR_PAGES     _IOW(ALLOCATOR_TYPE,3,int)

#define _IOC_MAX_NR 3
#endif
