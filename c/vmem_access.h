#include "vmem_parser.h"

struct lock_container{
      // TODO: rename these items sensibly
      pid_t first;
      struct /* pair of void* pair int string*/ lock_pair{
            void* first;
            struct int_string_pair{
                  int first;
                  char* second;
            }second;
      }second;
};

struct addr_int_pair{
      void* first;
      int second;
};

struct addr_str_pair{
      void* first;
      char* second;
};

struct mem_map{
      struct mem_rgn mapped_rgn;
      struct addr_int_pair* mmap;
      struct addr_str_pair* cp_mmap;
      // only one will be initialized at any given moment
      // both are included in this struct to simplify code
      pid_t pid;
      long size;
};

void free_mem_map(struct mem_map* mmap, bool integers);
int* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e);
int read_single_val_from_pid_mem(pid_t pid, int bytes, void* vm);
char* read_str_from_mem_block_slow(pid_t pid, void* mb_start, void* mb_end);
bool write_int_to_pid_mem(pid_t pid, void* vm, int value);
bool write_str_to_pid_mem(pid_t pid, void* vm, const char* str);
void populate_mem_map(struct mem_map* mmap, pid_t pid, int d_rgn, bool use_additional_rgns, bool integers);
void update_mem_map(struct mem_map* mem, bool integers);
void narrow_mem_map_int(struct mem_map* mem, int match);
void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact);
