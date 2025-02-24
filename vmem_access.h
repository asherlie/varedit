#include "vmem_parser.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#define MEMCARVE_VER "libmemcarve 1.8.6"

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
      int* value;
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

struct int_blk{
      BYTE* stack;
      BYTE* heap;
      BYTE** addtnl;
      unsigned char n_ad;
};

struct mem_map{
      struct mem_rgn mapped_rgn;
      struct addr_int_pair* i_mmap;
      struct i_mmap_map i_mmap_hash;
      struct addr_str_pair* s_mmap;
      struct str_blk* blk;
      struct int_blk* i_blk;
      unsigned int i_size, s_size, size;
      int d_rgn, int_mode_bytes;
      bool integers, low_mem, force_block_str, use_addtnl;
};

/* ~~~~~~~~~~~~~~~~begin optimized feb 2025 changes~~~~~~~~~~~~~~~~~ */

struct found_variable{
    uint8_t* address;
    uint8_t len;

    struct found_variable* next, * prev;
};

/*
 * there are benefits and drawbacks to using a LL - it's nice because narrowing is easier. can just redirect pointers
 * and free up memory, as opposed to keeping memory allocated
 *
 * array would occupy less total memory at its peak, however
 * try LL for now.
 */
// TODO: make this a lock free linked list
struct narrow_frame{
    char label[16];
    // TODO: are these parens right?
    // TODO: tracked variables must be abled to get linked back to the vmem of external process
    _Atomic (struct found_variable*) tracked_vars;
    _Atomic int n_tracked;

    // TODO: initialize this
    pthread_mutex_t lock;
};

// we'll be using named frames to keep track of different collections of tracked variables
// this way we can effortlessly switch between them
// to keep track of a variable we need a pointer into a region and size of that variable
struct mem_map_optimized{
    struct mem_rgn rgn;
    uint8_t* heap;
    uint8_t* stack;
    uint8_t** other;
    // use rgn data for this
    //uint8_t n_other;

    struct narrow_frame* frames;
    int n_frames, frame_cap;
};

void populate_mem_map_opt(struct mem_map_optimized* m, _Bool stack, _Bool heap, _Bool other);
void insert_frame_var(struct narrow_frame* frame, uint8_t* address, uint8_t len);
uint64_t narrow_mem_map_frame_opt_subroutine(struct narrow_frame* frame, uint8_t* start_rgn, uint8_t* end_rgn, void* value, uint16_t valsz);
void narrow_mem_map_frame_opt(struct mem_map_optimized* m, struct narrow_frame* frame, uint8_t n_threads, void* value, uint16_t valsz, 
                              _Bool* heap_match, _Bool* stack_match, _Bool* other_match);
void init_mem_map_opt(struct mem_map_optimized* m);
void add_frame(struct mem_map_optimized* m, char* label);

/* ~~~~~~~~~~~~~~~~end optimized feb 2025 changes~~~~~~~~~~~~~~~~~ */

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
void flatten_i_mmap_hash(struct mem_map* mem);
_Bool regularize_i_mmap_hash(struct mem_map* mem);
void update_mem_map(struct mem_map* mem);
void narrow_mem_map_int(struct mem_map* mem, int match);
void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact_s, bool exact_e);
bool print_locks(struct lock_container* lc);
long remove_lock(struct lock_container* lc, unsigned int rm_s, bool keep_first, char free_op);
unsigned int free_locks(struct lock_container* lc, char free_op);
unsigned long lock_th(struct lock_container* lc);
struct lock_container* lock_container_init(struct lock_container* lc, unsigned int initial_sz);
bool create_lock(struct lock_container* lc, pid_t pid, void** addr, int* i_val, char** s_val, unsigned int n_addr, bool mul_val, bool integers);
