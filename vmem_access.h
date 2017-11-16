#include "vmem_parser.h"

int* read_ints_from_pid_mem(pid_t pid, void* vm_s, void* vm_e);
int read_int_from_pid_mem(pid_t pid, void* vm);
char read_char_from_pid_mem(pid_t pid, void* vm);
std::string read_str_from_mem_block(pid_t pid, void* mb_start, void* mb_end);
bool write_int_to_pid_mem(pid_t pid, void* vm, int value);
bool write_str_to_pid_mem(pid_t pid, void* vm, std::string str);
mem_map vars_in_mem(pid_t pid, int d_rgn, bool additiona, bool integers);
void update_mem_map(mem_map &mem, bool integers);
void narrow_mem_map_int(mem_map &mem, int match);
void narrow_mem_map_str(mem_map &mem, std::string match, bool exact);
