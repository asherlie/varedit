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

struct str_blk{
      bool in_place;
      char* stack;
      char* heap;
      char** addtnl;
      unsigned char n_ad;
};

struct mem_map{
      struct mem_rgn mapped_rgn;
      struct addr_int_pair* mmap;
      struct addr_str_pair* cp_mmap;
      struct str_blk blk;
      // only one will be initialized at any given moment
      // both are included in this struct to simplify code
      pid_t pid;
      int d_rgn;
      unsigned int size;
      int int_mode_bytes;
      bool use_addtnl;
};

void free_mem_map(struct mem_map* mmap, bool integers);
bool read_bytes_from_pid_mem_dir(void* dest, pid_t pid, int bytes, void* vm_s, void* vm_e);
BYTE* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e);
int read_single_val_from_pid_mem(pid_t pid, int bytes, void* vm);
char* read_str_from_mem_range(pid_t pid, void* mb_start, int len);
char* read_str_from_mem_range_slow_dir(char* dest, pid_t pid, void* mb_start, int min_strlen, void* last_avail);
char* read_str_from_mem_range_slow(pid_t pid, void* mb_start, void* mb_end);
bool pid_memcpy(pid_t dest_pid, pid_t src_pid, void* dest, void* src, int n_bytes);
bool write_bytes_to_pid_mem(pid_t pid, int bytes, void* vm, BYTE* value);
bool write_int_to_pid_mem(pid_t pid, void* vm, int value);
bool write_str_to_pid_mem(pid_t pid, void* vm, const char* str);
void populate_mem_map(struct mem_map* mmap, pid_t pid, int d_rgn, bool use_additional_rgns, bool integers, int bytes);
void update_mem_map(struct mem_map* mem, bool integers);
void narrow_mem_map_int(struct mem_map* mem, int match);
void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact);
