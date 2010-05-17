/*Contiguous Memory Allocator for Modules*/
#include <linux/kernel.h>
#include<linux/module.h>
#include<linux/init.h>
#include<linux/sched.h>
#include<linux/mm.h>
#include<linux/fs.h>
#include<asm/page.h>
#include<asm/pgtable-2level.h>
#include<linux/mmzone.h>
#include <linux/list.h>
#include <linux/slab.h>
#include<linux/vmalloc.h>
#include<linux/wrapper.h>
#include "allocator.h"
#include "my_vmalloc.h"

MODULE_AUTHOR("A.R.Karthick");
MODULE_DESCRIPTION("Contiguous Memory Allocator:");
MODULE_PARM(percent_high,"i");
MODULE_LICENSE("GPL");

#define TARGET_PAGES 100
#define PERCENT_HIGH percent_high
#define CLUSTER_SIZE_HIGH ( (PERCENT_HIGH * TARGET_PAGES)/100 )
#define CLUSTER_SIZE CLUSTER_SIZE_HIGH
#define EXTRA_PAGES  20
#define REAL_PAGES   (TARGET_PAGES + EXTRA_PAGES)


#define CONTIGUOUS_PERCENT(size)     ( ( (size) * 100 )/TARGET_PAGES )

#ifdef CUSTOM_VMALLOC

#if TARGET_PAGES <= MY_VMALLOC_LIMIT    //to be safe

 #define vmalloc my_vmalloc
 #define vfree   my_vfree

#endif

#endif

static int percent_high;
extern unsigned int nr_free_pages();
/* Have a contiguous structure sorted by Address */


LIST_HEAD(v_contiguous_list); //define a LIST head for our contiguous pages

static int nr_v_contiguous;
static int nr_r_contiguous;

static unsigned int vmalloc_addr;
static int index;
struct contiguous_page *v_contiguous_array[REAL_PAGES]; //possible contiguous allocations

struct contiguous_page *r_contiguous_array[REAL_PAGES]; //possible real contiguous allocations

/*Release the v_contiguous list */

static void destroy_contiguous(struct list_head *head) {
  struct list_head  *traverse,*next;
  for(traverse = head->next; traverse != head; traverse = next) {
    struct contiguous_page *page;
    next = traverse->next;
    page = list_entry(traverse,struct contiguous_page,v_contiguous_list);
    kfree(page);
  }
}

/* Scan the List.Sort the list by Address */

static void sort_list_add(struct contiguous_page *ptr) {
    struct list_head *head = &v_contiguous_list;
    struct list_head *traverse;
    list_for_each(traverse,head) {
        struct list_head *temp = traverse; 
        struct contiguous_page *page = list_entry(temp,struct contiguous_page,v_contiguous_list); 
        //get the value of the contiguous page and then do a sort
        if(ptr->address < page->address) { 
            /*Add before this Entry */
            list_add_tail(&ptr->v_contiguous_list,temp);//add between temp->prev and temp
            goto out;
        }
    }
    //we are here when werent added.Add to the Tail of the List
    list_add_tail(&ptr->v_contiguous_list,&v_contiguous_list);
    out:
    return ;
}
 

/* Management of the List. Allocation of Contiguous List structure and insertion into the List Head */

static int insert_contiguous(struct page *page,unsigned long vmalloc_address) {
  struct contiguous_page *ptr ;
  unsigned long address = PAGE_VIRTUAL(page);
  if( ! (ptr = (struct contiguous_page *)kmalloc(sizeof(*ptr),GFP_KERNEL ) ) ) {
    goto out;
  }
  ptr->page = page; 
  ptr->address = address;
  ptr->vmalloc_address = vmalloc_address;
  sort_list_add(ptr); //sort and add into list
  return 1;
 out :
#ifdef DEBUG
    printk(KERN_ALERT "Error in Allocating memory for Contiguous structure:\n");
#endif 
   return 0;
}    

void display_contiguous(struct contiguous_page **page,int nr_contiguous) {
    int i;
    for(i=0;i<nr_contiguous;++i) {
        printk(KERN_ALERT "Contiguous:Address of Page #(%d)=%lx\n",i+1,page[i]->address);
    }
}

static void set_v_contiguous(struct list_head *head) {
  struct list_head *traverse ;
  nr_v_contiguous = 0;
  list_for_each(traverse,head) {
    struct contiguous_page *page = list_entry(traverse,struct contiguous_page,v_contiguous_list);
    v_contiguous_array[nr_v_contiguous++] = page;
  }
}

/*Now scan the Contiguous List for Contiguous Pages */
/* This routine will scan the list of Virtual Sorted Contiguous Pages and align the Contiguous Pages into the List.
*/


static int scan_contiguous(int target) {
    int start_index = 0,i=0,j;//start at Index 0 and hunt for CLUSTER_SIZE
    int cluster_size = CLUSTER_SIZE;
    nr_r_contiguous = 0;
    index = -1;
    if(nr_v_contiguous >= cluster_size) {
#ifdef DEBUG
        printk(KERN_ALERT "Entering a Scan of V Contiguous to convert into R contiguous:\n");
#endif

        rescan: 
        start_index = i; 
        /*start the hunt for cluster size pages.*/
        if(start_index + cluster_size - 1 < target ) {
            for( j=0,i = start_index; i < start_index + cluster_size - 1;  ++i,++j) {
                /*Note that we are hunting for a Cluster of Contiguous Pages.
                  Hence there is no allowance on the difference computed.
                  Purely PAGE_SIZE.
                */
                struct contiguous_page *one = v_contiguous_array[i];
                struct contiguous_page *two = v_contiguous_array[i+1]; //second page
                /*Now check for the Difference in the Virtual address of these two pages */
                if(two->address - one->address == PAGE_SIZE) {
                    /*Found a contiguous pair.Insert temporarily into the List */
                    if(i == start_index) {
                        /*Insert 2 in first Pass */
                        r_contiguous_array[j] =   one;
                        r_contiguous_array[j+1] = two;
                    } else {
                        r_contiguous_array[j+1] = two;//allocate the second One as the previous would have been already stored.
                    }
                } else { //Failure to get one Contiguous allocation.Rescan if necessary
                    i = i+1; //rescan from the Next point
                    goto rescan; 
                }      
            }
            if(j == cluster_size - 1) { //bulls EYE
                //Now we have the Required Contiguous Nr. of Pages.
                index = start_index;
                nr_r_contiguous = cluster_size;
                if(cluster_size == 1) {
                    r_contiguous_array[0] = v_contiguous_array[0];
                }     
                printk(KERN_ALERT "Percentage of Contiguous Pages=%d\n",CONTIGUOUS_PERCENT(cluster_size) );
            }
        }
    }
    return nr_r_contiguous > 0;
}  


/*Fill up a vmalloced address and try fetching a contiguous list from it.*/


static __inline__ int  count_free_pages() {
  //Loop through the mem_map array to get the free pages
  int i,free=0;
  for(i=0;i< num_physpages;++i) 
    if(PageInactiveClean(mem_map + i) ) ++free;

  return free;
}

/*Hunt for Contiguous pages Among the Non Contiguous Pages. */
/*Do a Page table Lookup for a Vmalloced page in the Kernels Page tables */

static struct page *lookup_page_table(const struct mm_struct *mm,unsigned int address,int clear) {
    pgd_t *pgd;
    pmd_t *pmd;
    pte_t *pte;
    struct page *page = NOPAGE_SIGBUS;//default no PAGE
    pgd = pgd_offset(mm,address);
    if(! pgd_none(*pgd) ) {
        /*Go  for a PMD lookup */
        pmd = pmd_offset(pgd,address);
        if( ! pmd_none(*pmd) ) {
            pte = pte_offset(pmd,address); //get the Pte entry 
            if(pte_present(*pte) ) {
                page = pte_page(*pte); //get the page from the entry
                if(clear && VALID_PAGE(page) && (!PageReserved(page) ) ) {
                    pte_t x = ptep_get_and_clear(pte); //clear the pte
                    (void)x;
#ifdef DEBUG_NOT_NOW
                    printk(KERN_ALERT "Non Contiguous Pages getting cleared off the List(%lx):\n",PAGE_VIRTUAL(page)); 
#endif
                    __free_page(page); //free of the page
                    page = (struct page *) 1;
                }
            }
        }
    }
    return page;
}

static void *my_alloc_pages(int nr) {
  //First grab non contiguous pages of this Order 
  unsigned int size = PAGE_SIZE * nr;
  return (void *)(vmalloc_addr = (unsigned int)vmalloc(size) ); //do a vmalloc first and get the pages
}


/*Free up the page table Entry */
static void free_page_table(unsigned long address) {
  struct page *page = lookup_page_table(&init_mm,address,1); //clear off the pte entry and free it off iff Unreserved and Valid
  if(page != (struct page *)1) 
    printk(KERN_ALERT "Bug: Unable to Free up a Page table Entry\n");
}

/* Reduce Internal Fragmentation.Free up the Non Contiguous List of Pages. */
void my_free(int index) {
  int i;
  for(i=0;i<nr_v_contiguous;++i) {
    if(i < index || i >= index + nr_r_contiguous) {
      free_page_table(v_contiguous_array[i]->vmalloc_address); //free page and clear page table entry
    }
  }
}

static void my_free_pages(unsigned int address) {
#if 0 //you can ask vmalloc to do this work
  { 
    int i;
  for(i=0;i<nr_r_contiguous;++i) 
    free_page_table(r_contiguous_array[i]->vmalloc_address);
  }
#endif

  if(address)
   vfree((void *)address);

  return ;
}

static void reserve_pages(struct contiguous_page **array,int nr,int flag) { 
  int i;
  for(i=0;i<nr;++i) {
    struct page *page = array[i]->page;
    if(flag) {
          atomic_inc(&page->count);
          mem_map_reserve(page);
    }else {
      atomic_dec(&page->count);
      mem_map_unreserve(page); //unreserve
  }
  }
}

static unsigned int fill_contiguous(int target) {
  unsigned int address =(unsigned int) my_alloc_pages(target); //get the pages
  unsigned int i;
  int status = 0;
  if(! address) {
    printk(KERN_ALERT "Unable to Vmalloc:\n");
    goto out;
  }
  /*Now lookup the Page Tables for vmalloced address,and push the pages into the List.*/
  for(i=address; i < (address + (target * PAGE_SIZE)); i += PAGE_SIZE) {
    struct page * page = lookup_page_table(&init_mm,i,0); //get the Page
    //push the Page into the Contiguous list
    if(page != NOPAGE_SIGBUS) {
      if(! insert_contiguous(page,i) ) {
        vfree((void*)address);
        goto out;
      }
    }
  }
  set_v_contiguous(&v_contiguous_list);

#ifdef DEBUG_NOT_NOW
  display_contiguous(v_contiguous_array,nr_v_contiguous);
#endif
  /*Once the Pages are in the Contiguous List,try converting them into a Real Contiguous List */

  if(scan_contiguous(TARGET_PAGES) ) {
    printk(KERN_ALERT "Successfull in Building %d Contiguous Pages:\n",nr_r_contiguous);

#ifdef DEBUG_NOT_NOW
  display_contiguous(r_contiguous_array,nr_r_contiguous);
#endif
 
  }else {
    printk(KERN_ALERT "Unsuccessfull in Building %d Contiguous Pages:\n",CLUSTER_SIZE);
   status = 0;
   goto out;
  }
  status = 1;
out:
  return status;
}

static int __init mymodule(void) {
  int error =  1;
  int free_pages = nr_free_pages();//get the nr of free pages

#ifdef DEBUG_NOT_NOW
  printk(KERN_ALERT "Nr of Inactive Clean pages=%d\n",count_free_pages() );
#endif

if(free_pages > TARGET_PAGES) { //if there are available free pages
   if( !(fill_contiguous(TARGET_PAGES) ) )
    goto out_free;
#ifdef OOPS  
   if(nr_r_contiguous) 
     my_free(index); //free off the non contiguous part
#endif 
     reserve_pages(r_contiguous_array,nr_r_contiguous,1); //reserve
 
   if(allocator_initialise(r_contiguous_array,nr_r_contiguous)) {
#ifdef DEBUG
     printk(KERN_ALERT "Failed to initialise the allocator device:\n");
#endif
     goto out_free_allocator;
   }
 
   
}
  error = 0;
  goto out;
 out_free_allocator:
  allocator_cleanup(); //cleanup the allocator
  out_free:
  destroy_contiguous(&v_contiguous_list);
 out:
  return error;
}

static void __exit mymodule_cleanup(void) {
  allocator_cleanup(); //cleanup the allocator
  reserve_pages(r_contiguous_array,nr_r_contiguous,0); //unreserve
  destroy_contiguous(&v_contiguous_list);
  my_free_pages(vmalloc_addr);
  return ;
}

module_init(mymodule);
module_exit(mymodule_cleanup);



