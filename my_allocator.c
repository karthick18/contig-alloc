/* A memory based device giving user space guys access to contiguous memory */

#include <linux/kernel.h>
#include<linux/module.h>
#include<linux/sched.h>
#include<linux/fs.h>
#include<linux/mm.h>
#include<asm/semaphore.h>
#include<linux/fcntl.h>
#include<linux/errno.h>
#include<asm/atomic.h>
#include<linux/slab.h>
#include "allocator.h"
#include "my_vmalloc.h"
#include<asm/bitops.h>
#include<asm/uaccess.h>
#include<linux/init.h>
#include <linux/devfs_fs_kernel.h>

static int major ;
extern struct file_operations allocator_file_operations;
extern struct vm_operations_struct allocator_vm_operations;

struct my_allocator {
  /*Contigous list of pages */
  struct page **a_contiguous_ptr; 
  atomic_t a_nr_pages; //total pages for the allocator 
  wait_queue_head_t a_requests; //wait queue of requests made to this allocator
  unsigned char *a_free_map; //bitmap of available pages with the allocator
  unsigned int a_first_bit; //next available index in the allocator
  unsigned int a_last_bit; //last bit
  spinlock_t a_spinlock; //SMP safe for multiple opens of the device
};

/* A per process mmap descriptor.We release the process pages when
   the process does a close on the file descriptor.
  Save the pointer to the process private data.*/

struct memory_map { 
  int m_first_bit;
  int m_nr_pages;
}; 

static struct my_allocator my_allocator;

static int allocator_cmd(unsigned int cmd,struct file *fptr,struct mem_user *ptr) {
  struct mem_user mem_user;
  struct my_allocator *allocator = &my_allocator;
  struct memory_map *temp = (struct memory_map *)fptr->private_data; //get the memory map from the file data
  int err = -1;
  if(! temp) goto out;
  mem_user.m_first_bit = temp->m_first_bit;
  mem_user.m_nr_pages =  temp->m_nr_pages;
  if(temp->m_first_bit + temp->m_nr_pages - 1 > allocator->a_last_bit) 
    temp->m_nr_pages = allocator->a_last_bit - temp->m_first_bit + 1; //Paranoid
  //write the addresses of the pages into the structure
  {
    int i,j;
    for(j=0,i=temp->m_first_bit;i<temp->m_first_bit + temp->m_nr_pages;++i,++j) {
      unsigned long phys = __pa(PAGE_VIRTUAL(allocator->a_contiguous_ptr[i]) ); 
      if(put_user(phys,ptr->m_addresses + j) ) {
        printk(KERN_ALERT "ioctl:Error writing %ld into Process (%d)s space:\n",phys,current->pid);
      }
    }
  }
  if(copy_to_user(ptr,&mem_user,sizeof(struct mem_user) - sizeof(unsigned long *) ) ) {
    printk(KERN_ALERT "ioctl:Unable to copy to Process (%d)s space:\n",current->pid);
  }
  err = 0;
  out :
    return err;
}
    
 
int allocator_file_ioctl(struct inode *iptr,struct file *fptr,unsigned int cmd,unsigned long arg) { 
  int err = -1;
  struct mem_user *ptr =(struct mem_user *)arg;
  if(_IOC_TYPE(cmd) != ALLOCATOR_TYPE || _IOC_NR(cmd) > _IOC_MAX_NR) {
 #ifdef DEBUG
    printk(KERN_ALERT "ioctl: Invalid cmd(%d) for this device\n",cmd);
#endif
    goto out;
  }
  if(_IOC_DIR(cmd) & _IOC_WRITE) 
    err = ! access_ok(VERIFY_READ,ptr,_IOC_SIZE(cmd) );
  else if(_IOC_DIR(cmd) & _IOC_READ)
    err = ! access_ok(VERIFY_WRITE,ptr,_IOC_SIZE(cmd) );
  
  if(err) { err = -1; goto out; }

 
  switch(cmd) {

  case _IOC_ALLOCATOR_HARDRESET:
    while(MOD_IN_USE) 
      MOD_DEC_USE_COUNT;
    MOD_INC_USE_COUNT;
    break;

    /*Get the number of pages that are contiguous and which can be logically mapped,but which _may_ not be the right figure if others are also mapping*/

  case _IOC_ALLOCATOR_PAGES:
    {
      int nr_pages = atomic_read(&my_allocator.a_nr_pages);
      put_user(nr_pages,(int *)arg);
      break;     
    }

  case _IOC_ALLOCATOR_GETPAGES: //get the info. about the pages
  
    err = allocator_cmd(cmd,fptr,ptr); //send control commands to the allocator
    
    goto out;

  default :
    err = -ENOTTY;
    goto out;
  }
  err = 0;
 out:
  return err;
}

/* Open for the Device */
int allocator_file_open(struct inode *iptr,struct file *fptr) {
  int nr = MINOR(iptr->i_rdev);
  int err = -1;
  if(nr > 0) {
 #ifdef DEBUG
    printk(KERN_ALERT "Invalid Minor nr (%d) for the device:\n",nr);
#endif
    goto out ;
  }
  {
    struct memory_map *map = kmalloc(sizeof(struct memory_map),GFP_KERNEL);
    if(! map) {
#ifdef DEBUG
      printk(KERN_ALERT "Unable to allocator per process memory map:\n");
#endif
      goto out;
   } 
    memset(map,0,sizeof(struct memory_map) );
    fptr->private_data = (struct memory_map *)map; //make it point to the map
  }
  err =0;
  printk(KERN_ALERT "Device opened:\n");
  MOD_INC_USE_COUNT; 
 out:
  return err;
}
/*
 Release for the device.
 Make sure that you release the memory_map if it is mapped by the process.
*/
int allocator_file_release(struct inode *iptr,struct file *fptr) {
  int nr = MINOR(iptr->i_rdev);
  int err = -1;
  if(nr > 0) {
#ifdef DEBUG
    printk(KERN_ALERT "Invalid Minor Number(%d) for the device:\n",nr);
#endif
    goto out;
  }
  {
    //get the memory map for the process
    struct memory_map *map = (struct memory_map*)fptr->private_data;
    if(map && map->m_nr_pages) { //if the map exists and the nr. free pages also exists
#ifdef DEBUG
      printk(KERN_ALERT "Freeing up Contiguous Part (%d-%d)\n",map->m_first_bit,map->m_first_bit + map->m_nr_pages - 1);
#endif
      free_contiguous(&my_allocator,map->m_first_bit,map->m_nr_pages);//free the bits in the bitmap
      kfree(map); //free the per process map
    }  
  }
  err = 0;
  printk(KERN_ALERT "Device released:\n");
  out :
   MOD_DEC_USE_COUNT;
  return err;
}

static void allocator_mmap_open(struct vm_area_struct *area) {
   (void)area;
  MOD_INC_USE_COUNT;
}

static void allocator_mmap_close(struct vm_area_struct *area) {
  (void)area;
  MOD_DEC_USE_COUNT;
}
 
/* mmap for the device.This is the most important routine as this is the one that exposes the functionality of contiguous memory. */

/*Remap the pages into the process address space */
int remap_mmap(unsigned long virtual,unsigned long physical,unsigned long size,pgprot_t prot) {
  /*Note that remap_page_range doesnt work with unreserved pages.
   Make sure that the pages are reserved before mapping,else remap_page_range will fail.
  */
  int err = 1;
  if(remap_page_range(virtual,physical,size,prot) ) { 
#ifdef DEBUG 
    printk(KERN_ALERT "Remap page range failed in mmaping the process pages:\n");
#endif
    goto out; //remap page range failed
  } else {
#ifdef DEBUG
    printk(KERN_ALERT "Remap page range successful in mapping process pages from Phys(%#08lx) - (%#08lx):\n",physical,physical + size - PAGE_SIZE);
#endif
  }
  err = 0;
 out:
  return err;
}

int allocator_file_mmap( struct file *fptr,struct vm_area_struct *vm_area_struct) {
  unsigned long start = vm_area_struct->vm_start;
  unsigned long end   = vm_area_struct->vm_end;
  unsigned long size  = PAGE_ALIGN(end - start);
  unsigned long pages = size >> PAGE_SHIFT; //get the pages to allocate
  int err = -1,index = -1,status;
  struct my_allocator *ptr = &my_allocator;
  int available = total_free(ptr); //get the available nr. of free pages
  (void)vm_area_struct->vm_pgoff; //we dont use the page offset
  if(available < pages) { 
    //Oops!! We dont have pages to donate. Give up
  #ifdef DEBUG
    printk(KERN_ALERT "mmap:We dont have Pages to Donate you.Max. available now (%d):\n",available);
   #endif
   goto out;
  }
  /*We are here when we have a _chance_ of allocating contiguous pages. 
  Note that we have a chance,because pages might have been stolen within the contiguous zone by other processes using the device.Hence the term _chance_.
  */
 while((status = allocate_contiguous(ptr,pages,&index) ) ) {
    err = -EAGAIN;
    if((fptr->f_flags & O_NONBLOCK))
      goto out; //try again
    err = -ERESTARTSYS;
    if(wait_event_interruptible(ptr->a_requests,allocate_contiguous(ptr,pages,&index) == 0) ) 
      goto out; //restart the syscall.As the process was signalled
    //rescan  
  }
   
  //Bulls Eye !! We have the contiguous set of pages.
  //Remap the pages to the process address space and get lost.
  { 
    struct memory_map *map = (struct memory_map *)fptr->private_data;
    unsigned long physical = __pa(PAGE_VIRTUAL(ptr->a_contiguous_ptr[index]) ); //get the physical address of the page
    map->m_first_bit = index; //store the index
    map->m_nr_pages  = pages;
    if(remap_mmap(start,physical,size,vm_area_struct->vm_page_prot) ) 
      goto out; //failed to remap
    (void)allocator_mmap_open(vm_area_struct); //open the area
    vm_area_struct->vm_ops = &allocator_vm_operations;
    atomic_inc(&fptr->f_dentry->d_inode->i_count); //increment the inode reference count
  }
 
  printk(KERN_ALERT "mmap:Success in contiguous allocation of (%ld) for Process(%d):\n",pages,current->pid);

  err = 0;
  out :
    return err;
  }



/* 
 Clear off the bits in the free_map.
 */
void free_contiguous(struct my_allocator *ptr,int first_bit,int nr_pages) {
  unsigned char *map = ptr->a_free_map;
  if(map) { //be safe.Dont want any bizarre Oops!!
    spin_lock(&ptr->a_spinlock);
    if(first_bit + nr_pages - 1 > ptr->a_last_bit) {
      //Paranoid !! 
      nr_pages = ptr->a_last_bit - first_bit + 1; 
    }
    {
      int i;
      for(i=first_bit; i < first_bit + nr_pages ; ++i) 
        if(! test_and_clear_bit(i,map) ) {
        #ifdef DEBUG
	  printk(KERN_ALERT "Freeing off an already cleared map(%d):\n",i);
        #endif
	}
    }
    spin_unlock(&ptr->a_spinlock);
  }
  if(waitqueue_active(&ptr->a_requests) )
   wake_up_interruptible(&ptr->a_requests);
  return ;
}
      
unsigned char*allocate_free_map(unsigned char **map,int entries) {
  if(map) {
    unsigned int bytes;
    if(*map) 
      kfree(*map); //remove the old map
   
    bytes = FREE_BYTES(entries);

    
    if(! (*map = kmalloc(bytes,GFP_KERNEL) ) ) {
#ifdef DEBUG
      printk(KERN_ALERT "Unable to allocate the free bitmap:\n");
#endif
      goto out;
    }
    return *map;
  }
 out:
  return NULL;
}

void allocator_free_array(struct my_allocator *ptr) {
  if(ptr->a_contiguous_ptr)
   kfree(ptr->a_contiguous_ptr);
  return;
}
  
static struct page **allocator_alloc_array(struct my_allocator *ptr,int entries) {
   unsigned char *map;
   ptr->a_contiguous_ptr = kmalloc(sizeof(struct page *) * entries,GFP_KERNEL);
   if(! ptr->a_contiguous_ptr) goto out;
   if( ! (map = allocate_free_map(&ptr->a_free_map,entries) ) ) {
     kfree(ptr->a_contiguous_ptr); 
     ptr->a_contiguous_ptr = NULL;
     goto out;
   }
   return ptr->a_contiguous_ptr;
 out:
   return NULL;
 }

static void clear_map(unsigned char *map,int last_bit) {
  int i;
  for(i=0;i<=last_bit;++i) 
    clear_bit(i,map);
}
    

static int setup_contiguous(struct my_allocator *ptr,struct contiguous_page **array,int nr) {
  int i=0,err = 1,result;
  if((result = devfs_register_chrdev(major,DEVICE_NAME,&allocator_file_operations) ) < 0) {
   #ifdef DEBUG
    printk(KERN_ALERT "Unable to register Device %s\n",DEVICE_NAME);
   #endif
    goto out;
 }
  if(result) major = result; //store the major number
  printk(KERN_ALERT "Major Nr=%d\n",major);
  //set up the pages in the page array
  while(i < nr) {
   ptr->a_contiguous_ptr[i] = array[i]->page;
   ++i;
  }
 err = 0;
 out :
  return err ;
}

/*This routine would have to be called by the main contiguous allocator module
  after setting up the contiguous.
 If the hunt for contiguous allocation in the module fails,then this wont be called. 
*/

int allocator_initialise(struct contiguous_page **array,int new_entries) {
  struct my_allocator *ptr = &my_allocator;
  ptr->a_free_map = NULL; 
  if(!allocator_alloc_array(ptr,new_entries) ) {
#ifdef DEBUG 
    printk(KERN_ALERT "allocator: error in allocating space for the array:\n");
#endif
    goto out;
  } 
  atomic_set(&ptr->a_nr_pages,new_entries);

  /*Initialise other entities */
  
  init_waitqueue_head(&ptr->a_requests); //initialise the wait queue

  ptr->a_first_bit = 0;
  ptr->a_last_bit =  atomic_read(&ptr->a_nr_pages) - 1 ;
  clear_map(ptr->a_free_map,ptr->a_last_bit); //clear off the bits
  spin_lock_init(&ptr->a_spinlock);
  if(setup_contiguous(ptr,array,new_entries)) {
   #ifdef DEBUG
    printk(KERN_ALERT "Error in setting up contiguous area:\n");
  #endif
    goto out;
  }
  return 0;
 out:
  return 1;
}


int total_free(struct my_allocator *ptr) {
  unsigned char *map = ptr->a_free_map;
  int i=0,nr=0;

  for(i=0;i<=ptr->a_last_bit;++i) {
    if(! test_bit(i,map) ) {
      ++nr;
    }
  }
  return nr;
}


/*Allocator "nr" number of contiguous entries.Setup the Bitmap */

int allocate_contiguous(struct my_allocator *ptr,int nr,int *index) {
    unsigned char *map = ptr->a_free_map; //get the free map
    unsigned int first_bit = ptr->a_first_bit; 
    unsigned int nr_free = total_free(ptr); //get the total free pages
    int ret = 1;
    int scan_flag = first_bit ? 1 : 0;
    printk(KERN_ALERT "Total free=%d\n",nr_free);
    if(nr > ptr->a_last_bit + 1) goto out; //not possible
    if(nr_free < nr) goto out; //allocation not possible
    *index = -1; 
    spin_lock(&ptr->a_spinlock); //SMP safe BABY !!
    rescan:
    if(first_bit + nr - 1 <= ptr->a_last_bit) {  
        //hunt for a cluster of "nr" contiguous pages
        int i,j;
        for(j=0,i=first_bit; i < first_bit + nr && j < nr; ++i,++j) {
            if(test_bit(i,map) ) {
                first_bit = find_next_zero_bit(map,(ptr->a_last_bit - i ),i+1);
#ifdef DEBUG
                printk(KERN_ALERT "Next free bit #(%d)\n",first_bit);
#endif
                goto rescan; //start the rescan from the next free point
            }
        }
        if(j == nr)
        {
            int i;
#ifdef DEBUG
            printk(KERN_ALERT "Allocation granted from (%d) for (%d) pages:\n",first_bit,nr);
#endif
            *index = first_bit; //store the start index of the scan
            for(i=first_bit;i < first_bit + nr; ++i)
                set_bit(i,map); //set the bit
            ret = 0;
            ptr->a_first_bit = find_next_zero_bit(map,ptr->a_last_bit - i + 1,i);
#ifdef DEBUG
            printk(KERN_ALERT "Success:Next check at %d\n",ptr->a_first_bit);
#endif
            goto out;
        }else if(scan_flag) { //start the scan again if you havent started from the first bit equals 0 and you end up in a failure
            goto restart;
        }
    } else if(scan_flag) {
        restart:
        scan_flag = 0;
        first_bit = 0;
        goto rescan;
    }

    out :
    spin_unlock(&ptr->a_spinlock);
    return ret;
}

void allocator_cleanup() {
  struct my_allocator *ptr = &my_allocator;
  allocator_free_array(ptr);
  if(ptr->a_free_map)
   kfree(ptr->a_free_map);
  if(major)
   devfs_unregister_chrdev(major,DEVICE_NAME);
  return ;
}
struct file_operations allocator_file_operations = {
  open:allocator_file_open,
  release:allocator_file_release,
  mmap: allocator_file_mmap,
  ioctl: allocator_file_ioctl,
};

struct vm_operations_struct allocator_vm_operations = {
  open: allocator_mmap_open,
  close:allocator_mmap_close,
  /*No need for the page fault handler */
};


