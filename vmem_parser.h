#define STACK 0         
#define HEAP  1         
#define BOTH  2         
#define NONE  3         

#include <unistd.h>
#include <string>

// keeps track of virtual memory address ranges for heap, stack and remaining memory
struct mem_rgn{
      std::string p_name;

      void* heap_start_addr;
      void* heap_end_addr;

      void* stack_start_addr;
      void* stack_end_addr;

      int n_remaining;
      std::pair<void*, void*>* remaining_addr;
};

std::string which_rgn(mem_rgn rgn, void* addr);
mem_rgn get_vmem_locations(pid_t pid, bool unmarked_additional);
