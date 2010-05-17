#ifdef CUSTOM_VMALLOC
/*
  My own version of vmalloc to work around contiguous allocations through an LKM. Will have to be used by contiguous.c while doing an allocation instead of using vmalloc.
*/
#include <linux/kernel.h>
#include<linux/module.h>
#include<linux/slab.h>
#include<asm/pgtable.h>
#include<asm/pgtable-2level.h>
#include<asm/pgalloc.h>
#include "allocator.h"
#include "my_vmalloc.h"


/* Do a vmalloc allocation.First setup the List sorted by address
   and then setup the Page tables */

struct my_vmalloc_struct *my_vmalloc_head = NULL;
static int allocated_pages,freed_pages;

static int freeup_pte(pte_t *pte,unsigned long address,unsigned long end) {
  unsigned long start = address;
  int error =0;
  if(end <= start){
    error |= 1; 
    goto out;
  }
  start &= ~PMD_MASK;
  if(end > PMD_SIZE) end = PMD_SIZE;
 do {
    struct page *page;//the pointer to the page to be freed
    if(! pte_none(*pte) && pte_present(*pte) ) {
      pte_t x = ptep_get_and_clear(pte); //clear off the old pte
      page = pte_page(x); //get the page corresponding to the pte entry
      if(VALID_PAGE(page) && !(PageReserved(page) ) ) {
       #ifdef DEBUG_NOT_NOW
	printk(KERN_ALERT "My Vmalloc:Freeing up Page(%lx)\n",PAGE_VIRTUAL(page) );
       #endif
        ++freed_pages;
        __free_page(page);
      }
      else {
        printk(KERN_ALERT "Page(%lx) could not be freed:\n",PAGE_VIRTUAL(page) );
        error |= 0;
      }
    }
    ++pte;
    start += PAGE_SIZE;
  }while(start < end);
 out:
  return error ;
}

static int freeup_pmd(pmd_t *pmd,unsigned long address,unsigned long end) {
  unsigned long start = address;
  int error = 1;
  if(end <= start) 
    goto out;
   
  start &= ~PGDIR_MASK;
  if(end > PGDIR_SIZE) end = PGDIR_SIZE;
  do {
    pte_t *pte;
    if(! pmd_none(*pmd) ) {
      pte = pte_offset(pmd,start);
      if(freeup_pte(pte,start,end)) goto out;
    }
    start += PMD_SIZE;
    ++pmd;
  }while(start < end);
  error = 0;
  out :
    return error;
}
  

static int freeup_pgd(unsigned long address,unsigned long size) {
  unsigned long start = address;
  unsigned long end = address + size;
  int error = 1;
  pgd_t *pgd;
  if(end <= start) 
    goto out;
  freed_pages = 0;
  pgd = pgd_offset_k(address); //get the pgd pointer from the kernels page tables
  do {
    pmd_t *pmd;
    if(! pgd_none(*pgd) ) {
      pmd = pmd_offset(pgd,start);
     //free up the pmd entries
      if(freeup_pmd(pmd,start,end)) goto out;
    }
    start = (start + PGDIR_SIZE) & PGDIR_MASK;
    ++pgd;
  }while( start < end);
  error = 0; 
#ifdef DEBUG
  printk(KERN_ALERT "Total Freed Pages=%d\n",freed_pages);
#endif
 out:
  return error ;
}
   

/*Freeup the Page tables.Done on a myvfree */

/*Set up the Page tables */

static int setup_pte(pte_t *pte,unsigned long address,unsigned long end,pgprot_t prot) {
  unsigned long start = address;
  int error = 1;
  if( end <= start)
    goto out;
  start &= ~PMD_MASK; //align on a PMD boundary
  if( end > PMD_SIZE) 
    end = PMD_SIZE; //restrict to PMD_SIZE
  do {
    //start setting up the Page tables.
    /*Get a free page, and then setup the page tables */
    struct page *page;
    if(! pte_none(*pte) && pte_present(*pte) ) {
      page = pte_page(*pte);
      if(VALID_PAGE(page) && !(PageReserved(page) ) )
	__free_page(page); //free up the Page
    }
    //now allocate a new pte entry, by mapping a new page
    page = alloc_page(GFP_KERNEL); //allocate a zero order PAGE
    if(! page) {
#ifdef DEBUG
      printk(KERN_ALERT "Failed to allocate a Page:\n");
#endif
     goto out;
    } else {
      ++allocated_pages;
  #ifdef DEBUG_NOT_NOW
      printk(KERN_ALERT "My Vmalloc:Allocating Page(%lx)\n",PAGE_VIRTUAL(page) );
  #endif
    }
    
    //setup the PTE entry for this page
    set_pte(pte,mk_pte(page,prot) ); //make a pte entry and setup the page tables
    start += PAGE_SIZE;
    ++pte;
  }while( start < end);
  error = 0;  
 out:
  return error ;
}  

static int setup_pmd(pmd_t *pmd,unsigned long address,unsigned long end,pgprot_t prot) {
  unsigned long start = address;
  int error = 1;
  if(end <= start) 
    goto out;
  if(end > PGDIR_SIZE) end = PGDIR_SIZE;
  start &= ~PGDIR_MASK; 
  //now setup the PMD and PTE entries.Note that each PMD can in turn hold 1024 PTE entries
  do {
    pte_t *pte;
    if(pmd_none(*pmd)) { 
      pte = my_pte_alloc(pmd,start);
      if(! pte) {
       goto out;
      }
    }
    else
      pte = pte_offset(pmd,start);
    if(setup_pte(pte,start,end,prot)) goto out;
    start += PMD_SIZE;
    ++pmd; //get to the next pmd entry
  
  }while(start < end) ;
  error = 0;
 out:  
  return error;
}

static int setup_pgd(unsigned long address,unsigned long size,pgprot_t prot) {
  unsigned long start= address;
  unsigned long end = start + size;
  pgd_t *pgd;
  int error = 1;
  if(end <= start) 
    goto out;
  allocated_pages = 0;
  //get the PGD pointer from the Kernels page tables
  pgd = pgd_offset_k(address); 
  do {
    pmd_t *pmd;
    if(pgd_none(*pgd) ) { 
      pmd = my_pmd_alloc(pgd,start); //allocate a PMD entry
      if(! pmd) 
        goto out;
    }
    else
      pmd = pmd_offset(pgd,start);

    if( setup_pmd(pmd,start,end,prot) ) goto out;//set up a PMD entry

    start = (start + PGDIR_SIZE) & PGDIR_MASK;

    ++pgd ;

  }while(start < end);
 error = 0;
#ifdef DEBUG
 printk(KERN_ALERT "Total Allocated Pages=%d\n",allocated_pages);
#endif
 out:
  return error ;
}

static struct my_vmalloc_struct *my_vmfree(unsigned long address) {
  struct my_vmalloc_struct **ptr;
  struct my_vmalloc_struct *area;
  for(ptr = &my_vmalloc_head; (area = *ptr); ptr= &(*ptr)->next) {
    if(area->address == address) {
      *ptr = area->next;
      return area;
    }
  }

  printk(KERN_ALERT "Unable to find vmalloc area (%lx):\n",address);
  return NULL;
  }

static struct my_vmalloc_struct *my_vmarea(unsigned long size, pgprot_t prot) {
  struct my_vmalloc_struct **ptr; 
  struct my_vmalloc_struct *area; //vmalloc area to be allocated
  struct my_vmalloc_struct *myarea = NULL;
  unsigned long address = MY_VMALLOC_START; //start of the vmalloced area
  /* Leave a PAGE_SIZE hole to trap memory overwrites */
  size += PAGE_SIZE;
  for(ptr = &my_vmalloc_head; (area = *ptr); ptr = &(*ptr)->next ) {
    if(size + address < address) {
      return NULL;
    }
    /*Sort the addresss list */
    if(address + size < area->address) {
      // found a free slot 
      goto found;
    }
    address = area->address + size;
    if(address + size > MY_VMALLOC_END) 
      return NULL; //not found
  }

  found :
    //we are here when we are successful in finding a virtual address
    if( ! (myarea =  kmalloc(sizeof(*myarea),GFP_KERNEL) ) )
      return NULL;

#ifdef DEBUG
  printk(KERN_ALERT "Found a vmarea with address(%#08lx)\n",address);
#endif
   
  myarea->address = address;
  myarea->prot = prot;
  myarea->size = size;
  myarea->next = *ptr;
  *ptr = myarea; //set up the List

  return myarea;
}

void *my_vmalloc(unsigned long size) {
    struct my_vmalloc_struct *area;

    size = PAGE_ALIGN(size); //page align the size
 
    if(! size || (size >> PAGE_SHIFT) > num_physpages) 
        return NULL;

    if(! (area = my_vmarea(size,PAGE_KERNEL) ) ) 
        return NULL;
  
    //got the area.Setup the Page tables
  
    if(setup_pgd(area->address,size,PAGE_KERNEL)) {
        printk(KERN_ALERT "Failed to setup the page tables:\n");
        freeup_pgd(area->address,size); //freeup the pgd if they were set
        return NULL;
    }

    return (void *)area->address; //return the address

}

 void *my_vfree(void *address) {
   struct my_vmalloc_struct *area;  
   if(address) {
    if((area = my_vmfree((unsigned long)address) ) ) {
     //if the address exists in the list
     freeup_pgd(area->address,area->size - PAGE_SIZE);
     kfree(area); //freeup the area
     return address;
   }
   }
   return NULL;
 }



#endif
