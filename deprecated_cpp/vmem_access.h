#include "vmem_parser.h"

struct mem_map{
      mem_rgn mapped_rgn;
      std::pair<void*, int>* mmap;
      std::pair<void*, std::string>* cp_mmap;
      // only one std::pair will be initialized at any given moment
      // both are included in this struct to simplify code
      pid_t pid;
      long size;
};

int* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e);
int read_single_val_from_pid_mem(pid_t pid, int bytes, void* vm);
std::string read_str_from_mem_block_slow(pid_t pid, void* mb_start, void* mb_end);
bool write_int_to_pid_mem(pid_t pid, void* vm, int value);
bool write_str_to_pid_mem(pid_t pid, void* vm, std::string str);
void populate_mem_map(mem_map &mmap, pid_t pid, int d_rgn, bool use_additional_rgns, bool integers, int bytes);
void update_mem_map(mem_map &mem, bool integers, int bytes);
void narrow_mem_map_int(mem_map &mem, int match);
void narrow_mem_map_str(mem_map &mem, std::string match, bool exact);