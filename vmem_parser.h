#include <unistd.h>
#include <map>

struct mem_rgn{
      void* heap_start_addr;
      void* heap_end_addr;

      void* stack_start_addr;
      void* stack_end_addr;
};

// this struct is not a part of vmem_parser.cpp
// but is used to keep track of pids with the mmaps
struct mem_map{
      std::map<void*, int> mmap;
      pid_t pid;
};

mem_rgn get_vmem_locations(pid_t pid);
