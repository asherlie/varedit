#define STACK 0         
#define HEAP  1         
#define BOTH  2         
#define NONE  3         

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

struct m_addr_pair{
      void* first;
      void* second;
};

// keeps track of virtual memory address ranges for heap, stack and remaining memory
struct mem_rgn{
      const char* p_name;

      void* heap_start_addr;
      void* heap_end_addr;

      void* stack_start_addr;
      void* stack_end_addr;

      int n_remaining;
      struct m_addr_pair* remaining_addr;
};

bool is_substr(const char* substr, const char* str);
struct mem_rgn get_vmem_locations(pid_t pid, bool unmarked_additional);
const char* which_rgn(struct mem_rgn rgn, void* addr);
