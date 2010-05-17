#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/mman.h>
#include<sys/ioctl.h>
#include<fcntl.h>
#include<unistd.h>
#include<stdarg.h>
#include "allocator_ioctl.h"
#define DEVICE_NAME "/dev/contiguous"
#define PAGE_SIZE (1 << 12)
#define MAP_SIZE(pages)  ((pages) * PAGE_SIZE)

#define BUFFER PAGE_SIZE

static __inline__ volatile void message(int exit_status,FILE *fptr,const char *fmt,...) {
  char buffer[BUFFER+1];
  va_list ptr;
  va_start (ptr,fmt);
  vsnprintf(buffer,BUFFER,fmt,ptr);
  va_end(ptr);
  fprintf(fptr,"%s\n",buffer);
  if(exit_status) 
    exit(exit_status);
  return ;
}   

int nr_pages(int fd) { 
  int pages = 0;
  if(ioctl(fd,_IOC_ALLOCATOR_PAGES,&pages) < 0) {
    message(0,stderr,"Error in issuing the ioctl call on %s\n",DEVICE_NAME);
    return 0;
  }
  return pages;
}

int do_getpages(int fd,unsigned char *map) {
  struct mem_user *mem_user = (struct mem_user *)map;//use the same location for fetching the info
  memset(mem_user,0,sizeof(*mem_user) );
  mem_user->m_addresses =(unsigned long *)( map + sizeof(*mem_user) ); //use the same map
  //do the ioctl
  if(ioctl(fd,_IOC_ALLOCATOR_GETPAGES,mem_user) < 0) {
    message(0,stderr,"Failed in performing the ioctl on %s",DEVICE_NAME);
    return 1;
  }
  message(0,stderr,"Start allocation for the Contiguous Page=%d",mem_user->m_first_bit);
  message(0,stderr,"Nr. of pages of Contiguous allocation=%d",mem_user->m_nr_pages);
  {
    int i=0;
    while(i < mem_user->m_nr_pages) {
      message(0,stderr,"Physical Address of the Page=(%#08lx)",mem_user->m_addresses[i++]);
    }
  }
  return 0;
}
 
int main(int argc,char **argv) {
  int fd,pages;
  unsigned char *map;
  if( (fd = open(DEVICE_NAME,O_RDWR) ) < 0 ) {
    message(0,stderr,"Unable to open memory device:");
    goto out;
  }
  pages = nr_pages(fd); //get the max nr. of pages that can be mapped
  if(pages <= 0) {
    message(0,stderr,"No pages can be mmapped from %s\n",DEVICE_NAME);
    close(fd);
    goto out;
  }
 #ifdef DEBUG
   message(0,stderr,"Map Size = (%d)\n",MAP_SIZE(pages) );
 #endif
  //mmap the device
  if((map = mmap(0,MAP_SIZE(pages),PROT_READ | PROT_WRITE,MAP_SHARED,fd,0) ) == MAP_FAILED) {
    message(0,stderr,"Failed to mmap the device:");
    goto out;
  } else {
    message(0,stderr,"Successfull in mmapping the device:");
  }
  
  do_getpages(fd,map);
  munmap(map,MAP_SIZE(pages));
  close(fd);
  return 0;
 out:
  return -1;
}
  


