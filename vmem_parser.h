#include <unistd.h>
#include <map>
#include <string>

struct mem_rgn{
      void* heap_start_addr;
      void* heap_end_addr;

      void* stack_start_addr;
      void* stack_end_addr;
};

// this struct is not a part of vmem_parser.cpp
// but is used to keep track of pids with the mmaps
struct mem_map{
      mem_rgn mapped_rgn;
      std::pair<void*, int>* mmap;
      std::pair<void*, std::string>* cp_mmap;
      pid_t pid;
      long size;
};

mem_rgn get_vmem_locations(pid_t pid);
