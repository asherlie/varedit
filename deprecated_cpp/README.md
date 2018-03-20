# varedit

- [x] varedit finds virtual memory addresses of variables in running processes

- [x] varedit edits these variables

### some examples of common usage
##### find the virtual memory address of a string containing "hello" in process with pid 487, searching in all available memory locations:
  ```
  sudo ./v 487 -p hello -E -C
  ```
##### write the integer 236 to memory location 0x7ff82 of process with pid 12692
  ```
  sudo ./v 12692 -w 0x7ff82 236
  ```
##### enter interactive mode on process with pid 139 looking for strings in all available memory
  ```
  sudo ./v 139 -f -E -C
  ```
  or just
  ```
  sudo ./v 139 -E -C
  ```
   
# vmem_access

vmem_access is a library created to make programs like varedit easier to write

vmem_access.h contains the following functions for reading and writing to virtual memory
* int* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e)
* int read_single_val_from_pid_mem(pid_t pid, int bytes, void* vm)
* std::string read_str_from_mem_block_slow(pid_t pid, void* mb_start, void* mb_end)
* bool write_int_to_pid_mem(pid_t pid, void* vm, int value)
* bool write_str_to_pid_mem(pid_t pid, void* vm, std::string str)
##### the following is a simple program that will print the value stored in the specified virtual memory location of the specified process id
```c++
#include <iostream>
#include "vmem_access.h"

int main(int argc, char* argv[]){
    pid_t pid = std::stoi(argv[1]);
    // memory addresses must be cast to void* to work with read_bytes_from_pid_mem
    void* mem_addr = (void*)strtoul(argv[2], 0, 16);
    // the final parameter of read_bytes_from_pid_mem is nullptr when reading a single value
    int* value = read_bytes_from_pid_mem(pid, 4, mem_addr, nullptr);
    std::cout << *value << std::endl;
    delete[] value;
}
```


the remaining functions defined in vmem_access.h are used for creating and manipulating `mem_map` objects defined in vmem_access.h
* void populate_mem_map(mem_map &mmap, pid_t pid, int d_rgn, bool use_additional_rgns, bool integers)
* void update_mem_map(mem_map &mem, bool integers)
* void narrow_mem_map_int(mem_map &mem, int match)
* void narrow_mem_map_str(mem_map &mem, std::string match, bool exact)

in order to use these functions, an initial `mem_map` object must be created, and its attribute `mapped_rgn` must be set using `get_vmem_locations(pid_t, bool)` defined in vmem_parser.h

`mem_map.mapped_rgn` is of type `mem_rgn`, which is defined in vmem_parser.h and contains the virtual memory address ranges of different sections of process memory.

The initialization and population of a `mem_map` object is demonstrated below, populating it with integers from both the stack and heap, as well as any additional memory regions that are found
```c++
// assuming pid_t pid = some valid process id 
mem_map vmem;
vmem.mapped_rgn = get_vmem_locations(pid, true);
// BOTH is a macro that indicates we will be searching both the stack and heap
populate_mem_map(vmem, pid, BOTH, true, true);
```
