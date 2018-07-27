#define NONE  -1
#define STACK  0
#define HEAP   1
#define BOTH   2

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

struct m_addr_pair{
      void* start;
      void* end;
};

// keeps track of virtual memory address ranges for heap, stack and remaining memory
struct mem_rgn{
      pid_t pid;
      char* p_name;
      struct m_addr_pair heap;
      struct m_addr_pair stack;
      int n_remaining;
      struct m_addr_pair* remaining_addr;
};

void free_mem_rgn(struct mem_rgn* mr);
bool is_substr(const char* substr, const char* str);
struct mem_rgn get_vmem_locations(pid_t pid, bool unmarked_additional);
const char* which_rgn(struct mem_rgn rgn, void* addr, int* res);
