#ifdef CUSTOM_VMALLOC
#ifndef _MY_VMALLOC_H
#define _MY_VMALLOC_H

/*Try using the virtual address space available from 0 to 3 GB. 
 The remaining is used by the Kernel for itself.
*/
#define MY_VMALLOC_START  0x1000 //start at First Page
/* Each pgdir entry takes 4 MB.Hence use 768 / 6 which is around
 130 Entries to cover a virtual address of around 130 * 4 MB which is roughly around
 500 MB, or around 1/2 GB.Should be safe with respect to Fixmap and PKMAP_BASE.*/

#define MY_VMALLOC_END    ((USER_PGD_PTRS * PGDIR_SIZE)/6)

#define MY_VMALLOC_LIMIT   3000 //limit of my_vmalloc usage.fall back to vmalloc for greater allocations

struct my_vmalloc_struct{
  unsigned long address; //virtual address
  unsigned long size; //the size of allocation
  pgprot_t prot; //the protection available for the virtual address
  struct my_vmalloc_struct *next; //the list head pointer
};

static __inline__ pmd_t *my_pmd_alloc(pgd_t *pgd,unsigned long address){
#if 0
  //Applicable for 64 bit architectures
  unsigned long page = (unsigned long)alloc_page(GFP_USER);
  if( ! page) 
    goto out;
  set_pgd(pgd,__pgd(_KERNPG_TABLE + __pa(page) ) );//set up the pgd entry
  return pmd_offset(pgd,address) ; 
 out:
  return (pmd_t *) NULL;
#else
  return (pmd_t*)pgd;
#endif
}
 
static __inline__ pte_t *my_pte_alloc(pmd_t *pmd,unsigned long address) {
  unsigned long page = (unsigned long)__get_free_page(GFP_KERNEL);
  unsigned long offset = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1) ;
  if(! page) 
    goto out;
  set_pmd(pmd,__pmd(_KERNPG_TABLE + __pa(page) ) ); 
  return (pte_t*)page + offset;
 out:
  return (pte_t*)NULL;
}

extern void *my_vmalloc(unsigned long size);
extern void *my_vfree(void *address);

#endif
#endif
