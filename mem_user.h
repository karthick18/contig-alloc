#ifndef _MEM_USER_H
#define _MEM_USER_H

struct mem_user {
  int m_first_bit;
  int m_nr_pages;
  unsigned long *m_addresses; //store the contiguous page addresses
};
#endif
