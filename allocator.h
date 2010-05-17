#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H


struct contiguous_page {
  struct list_head v_contiguous_list;
  unsigned long address; //the address of the page
  unsigned long vmalloc_address;//needed to free the vmalloced pages
  struct page *page;
};

#define FREE_BYTES(count)   ( ( ( (count) + 7 ) & ~7 )/8 )
#define DEVICE_NAME "contiguous"
#if 0
#define PAGE_VIRTUAL(page)         ((unsigned long) __va(( (page) - mem_map) * PAGE_SIZE))
#else
 #define PAGE_VIRTUAL(page)    ( (unsigned long)(page)->virtual )
#endif

struct my_allocator;

extern void free_contiguous(struct my_allocator *,int first,int last);
extern int allocate_contiguous(struct my_allocator *,int nr,int *index);
extern int total_free(struct my_allocator *);
extern int allocator_file_open(struct inode *,struct file *);
extern int allocator_file_close(struct inode *,struct file *);
extern int allocator_file_mmap(struct file *,struct vm_area_struct *);
extern int allocator_initialise(struct contiguous_page**,int nr);
extern void allocator_cleanup(void);
/*define the possible ioctls on the device */
#include "allocator_ioctl.h"

#endif
