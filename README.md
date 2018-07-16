# varedit

- [x] varedit finds virtual memory addresses of variables in running processes

- [x] varedit edits these variables

#### this section is devoted to varedit usage. vmem_access and vmem_parser library documentation begin [here](#vmem_access)

usage: ./v <pid> {[-p [filter]] [-r <memory address>] [-w <memory address> <value>] [-i] [-S] [-H] [-B] [-A] [-E] [-U] [-C] [-b <n bytes>] [-V] [-pr] [-pl <print limit>]}

&nbsp;&nbsp;&nbsp;&nbsp;-p&nbsp;&nbsp;:&nbsp;prints values in specified memory region with optional filter

&nbsp;&nbsp;&nbsp;&nbsp;-r&nbsp;&nbsp;:&nbsp;read single value from virtual memory address

&nbsp;&nbsp;&nbsp;&nbsp;-w&nbsp;&nbsp;:&nbsp;write single value to virtual memory address

&nbsp;&nbsp;&nbsp;&nbsp;-i&nbsp;&nbsp;:&nbsp;interactive mode (default)

&nbsp;&nbsp;&nbsp;&nbsp;-S&nbsp;&nbsp;:&nbsp;use stack (default)

&nbsp;&nbsp;&nbsp;&nbsp;-H&nbsp;&nbsp;:&nbsp;use heap

&nbsp;&nbsp;&nbsp;&nbsp;-B&nbsp;&nbsp;:&nbsp;use both heap and stack

&nbsp;&nbsp;&nbsp;&nbsp;-A&nbsp;&nbsp;:&nbsp;look for additional memory regions

&nbsp;&nbsp;&nbsp;&nbsp;-E&nbsp;&nbsp;:&nbsp;use all available memory regions

&nbsp;&nbsp;&nbsp;&nbsp;-U&nbsp;&nbsp;:&nbsp;use unmarked additional regions (very slow)

&nbsp;&nbsp;&nbsp;&nbsp;-C&nbsp;&nbsp;:&nbsp;use char/string mode

&nbsp;&nbsp;&nbsp;&nbsp;-b&nbsp;&nbsp;:&nbsp;set number of bytes to read at a time in integer mode

&nbsp;&nbsp;&nbsp;&nbsp;-V&nbsp;&nbsp;:&nbsp;verbose (enables print region and ignores result_print_limit)

&nbsp;&nbsp;&nbsp;&nbsp;-p&nbsp;&nbsp;:&nbsp;print region that memory addresses are found in

&nbsp;&nbsp;&nbsp;&nbsp;-pl&nbsp;:&nbsp;set print limit for search results (only affects interactive mode, can be useful for small screens)


### notes on varedit interactive mode behavior
* '?' can be entered at any time for mode specific help
* the delimeter '\\' can be used to search for '?', 'q', 'u', 'r' and 'w' in string mode
* a '^' in a search string acts as it would in a regular expression. it marks the beginning of a target string of our search, it will only accept exact matches to the start of a string
* "\\0" marks the end of a target string of our search, it will only accept exact matches to the end of a string
* because of the behavior of '^' and "\0" in search strings, the two can be used together to search for strings whose exact values are known, ^string\0, for example, can be used to find strings that exactly match "string". this search would be equivalent to the search foo^string\0bar
* a "\0" in any write string will be replaced with a NUL character unless escaped with a '\'. this can be used to shorten strings

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
  sudo ./v 139 -i -E -C
  ```
  or just
  ```
  sudo ./v 139 -E -C
  ```

# vmem_access

vmem_access is a library created to make programs like varedit easier to write

vmem_access relies on linux specific system calls and will not work on any other platform. all code using this library must be compiled with the flag -D_GNU_SOURCE



the boolean macros `LOW_MEM` and `FORCE_BLOCK_STR` change the behavior of functions that interact with `mem_map`s
### FORCE_BLOCK_STR

`FORCE_BLOCK_STR` defaults to true

`FORCE_BLOCK_STR` should be set to false only if you are using a computer with very little memory

the block string representation of strings is much faster than individually allocated strings but sacrifices memory

#### if FORCE_BLOCK_STR is enabled:
* strings are never individually allocated, instead, they are kept in large blocks by memory region and freed when possible
* `FORCE_BLOCK_STR` takes precedence over `LOW_MEM`. even if `LOW_MEM` is enabled, strings will never be individually allocated

### LOW_MEM

`LOW_MEM` defaults to false

`LOW_MEM` should be set to true only if you are using a computer with very little memory

#### if LOW_MEM is enabled:
* memory intensive integer mem_map optimizations are disabled
* if `FORCE_BLOCK_STR` is not enabled, strings are individually allocated
* otherwise, unused memory blocks containing strings are freed as soon as possible, sacrificing speed


#### vmem_access.h contains the following functions for reading and writing to virtual memory
* `bool read_bytes_from_pid_mem_dir(void* dest, pid_t pid, int bytes, void* vm_s, void* vm_e)`
* `BYTE* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e)` // BYTE* is unsigned char
* `int read_single_val_from_pid_mem(pid_t pid, int bytes, void* vm)`
* `char* read_str_from_mem_range(pid_t pid, void* mb_start, int len)`
* `char* read_str_from_mem_range_slow_dir(char* dest, pid_t pid, void* mb_start, int min_strlen, void* last_avail)`
* `char* read_str_from_mem_range_slow(pid_t pid, void* mb_start, void* mb_end)`
* `bool pid_memcpy(pid_t dest_pid, pid_t src_pid, void* dest, void* src, int n_bytes)`
* `bool write_bytes_to_pid_mem(pid_t pid, int bytes, void* vm, int value)`
* `bool write_int_to_pid_mem(pid_t pid, void* vm, int value)`
* `bool write_str_to_pid_mem(pid_t pid, void* vm, const char* str)`
##### the following is a simple program written using vmem_access.h that will print the value stored in the specified virtual memory location of the specified process id
```c
#include <stdio.h>
#include <string.h>
#include "vmem_access.h"

int main(int argc, char* argv[]){
    pid_t pid = atoi(argv[1]);
    // memory addresses must be cast to void* to work with read_bytes_from_pid_mem
    void* mem_addr = (void*)strtoul(argv[2], 0, 16);
    // the final parameter of read_bytes_from_pid_mem is NULL when reading a single value
    // read_bytes_from_pid_mem returns a pointer to a BYTE array obtained with malloc()
    // this memory should be freed using free()
    BYTE* bytes = read_bytes_from_pid_mem(pid, sizeof(int), mem_addr, NULL);
    int value = *((int*)bytes);
    // the below would also work
    // memcpy(&value, bytes, 4);
    printf("value: %i\n", value);
    // memory allocated by read_bytes_from_pid_mem should be freed
    free(bytes);
    return 0;
}
```

this can also be done using read_bytes_from_pid_mem_dir

```c
#include <stdio.h>
#include "vmem_access.h"

int main(int argc, char* argv[]){
      pid_t pid = atoi(argv[1]);
      void* mem_addr = (void*)strtoul(argv[2], 0, 16);
      int value = 0;
      read_bytes_from_pid_mem_dir(&value, pid, sizeof(int), mem_addr, NULL);
      printf("value: %i\n", value);
      return 0;
}
```

or with read_single_val_from_pid_mem

```c
#include <stdio.h>
#include "vmem_access.h"

int main(int argc, char* argv[]){
      pid_t pid = atoi(argv[1]);
      void* mem_addr = (void*)strtoul(argv[2], 0, 16);
      int value = read_single_val_from_pid_mem(pid, sizeof(int), mem_addr);
      printf("value: %i\n", value);
      return 0;
}
```

##### some examples of pid_memcpy usage are below
```c
// assuming pid_t src_pid = some valid process id

// assumptions for double array example:
//    void* addr_double_array = a memory location in src_pid pointing to an array of doubles
//    int n_doubles = the number of elements in the double array in src_pid
double arr[n_doubles];
// copying an array of doubles from src_pid:addr_double_array to arr
pid_memcpy(getpid(), src_pid, arr, addr_double_array, n_doubles*sizeof(double));

// assumption for struct mem_rgn example:
//    void* addr_mem_rgn = a memory location in src_pid pointing to a mem_rgn struct

struct mem_rgn rgn;
// copying a mem_rgn struct from src_pid:addr_mem_rgn to rgn
pid_memcpy(getpid(), src_pid, &rgn, addr_mem_rgn, sizeof(struct mem_rgn));
```

the following functions defined in vmem_access.h are used for creating and manipulating `mem_map` structs defined in vmem_access.h
* `void populate_mem_map(struct mem_map* mmap, int d_rgn, bool use_additional_rgns, bool integers, int bytes)`
* `void update_mem_map(struct mem_map* mem, bool integers)`
* `struct mem_map* mem_map_init(struct mem_map* mem, pid_t pid, bool unmarked_additional)`
* `void narrow_mem_map_int(struct mem_map* mem, int match)`
* `void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact_s, bool exact_e)`

in order to use these functions, an initial `mem_map` struct must be created and initialized using `mem_map_init`.
`mem_map_init` sets mem_map.size to 0 and populates mem_map.mapped_rgn.

if `mem_map_init`'s `mem` parameter is `NULL`, a new malloc'd mem_map struct will be returned. otherwise `mem_map_init` will return a pointer to `mem`.

`mem_map.mapped_rgn` is of type `mem_rgn`, which is defined in vmem_parser.h and contains the virtual memory address ranges of each section of process memory.

The initialization and population of a `mem_map` struct is demonstrated below, populating it with integers from both the stack and heap, as well as any additional memory regions that are found
```c
// assuming pid_t pid = some valid process id 
struct mem_map vmem;
mem_map_init(&vmem, pid, true);
// BOTH is a macro that indicates we will be searching both the stack and heap
populate_mem_map(&vmem, BOTH, true, true, sizeof(int));
free_mem_rgn(&vmem.mapped_rgn);
free_mem_map(&vmem, true);
```

the same can be achieved with the following code
```c
// assuming pid_t pid = some valid process id 
struct mem_map* vmem = mem_map_init(NULL, pid, true);
// BOTH is a macro that indicates we will be searching both the stack and heap
populate_mem_map(vmem, BOTH, true, true, sizeof(int));
free_mem_rgn(&vmem->mapped_rgn);
free_mem_map(vmem, true);
free(vmem);
```

the remaining functions defined in vmem_access.h are used for creating, removing, and keeping track of locks
* `bool print_locks(struct lock_container* lc, bool integers)`
* `int remove_lock(struct lock_container* lc, int rm_s, bool keep_first)`
* `int free_locks(struct lock_container* lc)`
* `struct lock_container* lock_container_init(struct lock_container* lc, unsigned char initial_sz)`
* `pid_t create_lock(struct lock_container* lc, pid_t pid, void** addr, int* i_val, char** s_val, unsigned int n_addr, bool mul_val, bool integers, void* f_o_r)`

to use these, an initial `lock_container` struct must be created and initialized using `lock_container_init`.
if `lock_container_init`'s `lc` parameter is `NULL`, a new malloc'd lock_container struct will be returned. otherwise `lock_container_init` will return a pointer to `lc`.

the initialization of a `lock_container` struct and the creation of a lock on address 0xffff17 of process 2819 to the value 62 is demonstrated below
```c
pid_t pid = 2819;
void* addr = 0xffff17;
// i_val can also be an array of integers
int i_val = 62;
struct lock_container lc;
lock_container_init(&lc, 1);
// to lock a string value, the s_val parameter is set to a char**, and i_val to NULL
create_lock(&lc, pid, &addr, &i_val, NULL, 1, false, true, NULL);
/* indices of items in the lock_container struct are adjusted so that
   remove_lock with 0 as its rm_s parameter will always be valid for a non empty lock_container */
// to remove this lock
remove_lock(&lc, 0, false);
```
