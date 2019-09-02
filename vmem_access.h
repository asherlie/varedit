#include "vmem_parser.h"

#include <pthread.h>

#define MEMCARVE_VER "libmemcarve 1.7.0"

typedef unsigned char BYTE;

struct lock_container{
      pthread_t thread;
      pthread_mutex_t mut_lck;
      struct lock_entry* locks;
      unsigned int cap, n, n_removed;
};

struct lock_entry{
      pid_t pid;
      bool rng, mul_val, integers, active;
      void** m_addr;
      unsigned int n_addr;
      int* i_val;
      char** s_val;
};

struct addr_int_pair{
      void* addr;
      int value;
};

struct addr_str_pair{
      void* addr;
      char* value;
};

struct i_mmap_map{
      int n_bux, * bucket_ref;
      struct addr_int_pair** i_buckets;
      _Bool in_place;
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
      struct addr_int_pair* i_mmap;
      struct i_mmap_map i_mmap_hash;
      struct addr_str_pair* s_mmap;
      struct str_blk* blk;
      unsigned int i_size, s_size, size;
      int d_rgn, int_mode_bytes;
      bool integers, low_mem, force_block_str, use_addtnl;
};

void free_mem_map(struct mem_map* mem);
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
struct mem_map* mem_map_init(struct mem_map* mem, pid_t pid, bool unmarked_additional);
bool set_mode_mem_map(struct mem_map* mem, bool integers);
void populate_mem_map(struct mem_map* mem, int d_rgn, bool use_additional_rgns, bool integers, int bytes);
void update_mem_map(struct mem_map* mem);
void narrow_mem_map_int(struct mem_map* mem, int match);
void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact_s, bool exact_e);
bool print_locks(struct lock_container* lc);
long remove_lock(struct lock_container* lc, unsigned int rm_s, bool keep_first, char free_op);
unsigned int free_locks(struct lock_container* lc, char free_op);
unsigned long lock_th(struct lock_container* lc);
struct lock_container* lock_container_init(struct lock_container* lc, unsigned int initial_sz);
bool create_lock(struct lock_container* lc, pid_t pid, void** addr, int* i_val, char** s_val, unsigned int n_addr, bool mul_val, bool integers);
