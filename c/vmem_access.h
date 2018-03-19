#include "vmem_parser.h"

typedef unsigned char BYTE;

struct lock_container{
      pid_t pid;
      void* m_addr;
      int i_value;
      char* s_value;
};

struct addr_int_pair{
      void* addr;
      int value;
};

struct addr_str_pair{
      void* addr;
      char* value;
};

struct mem_map{
      struct mem_rgn mapped_rgn;
      struct addr_int_pair* mmap;
      struct addr_str_pair* cp_mmap;
      // only one will be initialized at any given moment
      // both are included in this struct to simplify code
      pid_t pid;
      long size;
      int int_mode_bytes;
};

bool is_substr(const char* substr, const char* str);
void free_mem_map(struct mem_map* mmap, bool integers);
unsigned char* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e);
int read_single_val_from_pid_mem(pid_t pid, int bytes, void* vm);
char* read_str_from_mem_block(pid_t pid, void* mb_start, int len);
char* read_str_from_mem_block_slow(pid_t pid, void* mb_start, void* mb_end);
bool write_bytes_to_pid_mem(pid_t pid, int bytes, void* vm, int value);
bool write_int_to_pid_mem(pid_t pid, void* vm, int value);
bool write_str_to_pid_mem(pid_t pid, void* vm, const char* str);
void populate_mem_map(struct mem_map* mmap, pid_t pid, int d_rgn, bool use_additional_rgns, bool integers, int bytes);
void update_mem_map(struct mem_map* mem, bool integers);
void narrow_mem_map_int(struct mem_map* mem, int match);
void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact);
