#include "vmem_access.h"

#include <string.h>
#include <sys/uio.h>
#include <limits.h> // for IOV_MAX

#define STACK 0
#define HEAP 1
#define BOTH 2

// with less than 1000000 values, it is faster to do individual reads for integers when updating mem_map
#define RELOAD_CUTOFF 1000000
// if(LOW_MEM) update_mem_map optimizations for integers are turned off
#define LOW_MEM false

void free_mem_map(struct mem_map* mmap, bool integers){
      if(integers)free(mmap->mmap);
      else{
            for(unsigned long i = 0; i < mmap->size; ++i){
                  free(mmap->cp_mmap[i].value);
            }
            free(mmap->cp_mmap); 
      }
}

bool read_bytes_from_pid_mem_dir(void* dest, pid_t pid, int bytes, void* vm_s, void* vm_e){
      int sz_rgn;
      if(vm_e == NULL)sz_rgn = bytes;
      else sz_rgn = (char*)vm_e-(char*)vm_s;
      struct iovec local;
      struct iovec remote;
      local.iov_base = dest;
      local.iov_len = sz_rgn;
      remote.iov_base = vm_s;
      remote.iov_len = sz_rgn;
      ssize_t nread;
      nread = process_vm_readv(pid, &local, 1, &remote, 1, 0);
      return nread == sz_rgn;
}

BYTE* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e){
      int sz_rgn;
      if(vm_e == NULL)sz_rgn = bytes;
      else sz_rgn = (char*)vm_e-(char*)vm_s;
      BYTE* ret = malloc(sz_rgn);
      read_bytes_from_pid_mem_dir(ret, pid, bytes, vm_s, vm_e);
      return ret;
}

int read_single_val_from_pid_mem(pid_t pid, int bytes, void* vm){
      int ret = 0;
      read_bytes_from_pid_mem_dir(&ret, pid, bytes, vm, NULL);
      return ret;
}

char* read_str_from_mem_range(pid_t pid, void* mb_start, int len){
      return (char*)read_bytes_from_pid_mem(pid, 1, mb_start, (void*)((char*)mb_start+len));
}

// this function should only be used when string size is unknown
char* read_str_from_mem_range_slow(pid_t pid, void* mb_start, void* mb_end){
      char tmp;
      int str_sz = 1;
      char* ret = malloc(sizeof(char)*str_sz+1); int ret_p = 0;
      for(void* i = mb_start; i != mb_end; i = (void*)(((char*)i)+1)){ 
            tmp = (char)read_single_val_from_pid_mem(pid, 1, i);
            if(!(tmp > 0 && tmp < 127))return ret;
            if(ret_p == str_sz){
                  str_sz += 10;
                  char* tmp_ret = malloc(sizeof(char)*str_sz+1);
                  memset(tmp_ret+ret_p, '\0', str_sz-ret_p);
                  strcpy(tmp_ret, ret);
                  free(ret);
                  ret = tmp_ret;
            }
            ret[ret_p++] = tmp;
      }
      return ret;
}

bool pid_memcpy(pid_t dest_pid, pid_t src_pid, void* dest, void* src, int n_bytes){
      bool ret = true;
      BYTE* bytes;
      // don't use read_bytes_from_pid_mem to read from current process
      if(src_pid == getpid())bytes = (BYTE*)src;
      else bytes = read_bytes_from_pid_mem(src_pid, n_bytes, src, NULL);
      // don't use write_bytes_to_pid_mem to write to current process
      if(dest_pid == getpid())memcpy(dest, bytes, n_bytes);
      else ret = write_bytes_to_pid_mem(dest_pid, n_bytes, dest, bytes);
      if(src_pid != getpid())free(bytes);
      return ret;
}

bool write_bytes_to_pid_mem(pid_t pid, int bytes, void* vm, BYTE* value){
      struct iovec local;
      struct iovec remote;
      local.iov_base = value;
      local.iov_len = bytes;
      remote.iov_base = vm;
      remote.iov_len = bytes;
      return (bytes == process_vm_writev(pid, &local, 1, &remote, 1, 0));
}

// TODO: maybe allow this function to take in nbytes as in how much of the int do we wanna write
// this would avoid the need to memcpy ints to BYTE*s in varedit
bool write_int_to_pid_mem(pid_t pid, void* vm, int value){
      BYTE byte_int[4];
      memcpy(byte_int, &value, 4);
      return write_bytes_to_pid_mem(pid, 4, vm, byte_int);
}

bool write_str_to_pid_mem(pid_t pid, void* vm, const char* str){
      return write_bytes_to_pid_mem(pid, strlen(str), vm, (BYTE*)str);
}

void resize_str_mmap(struct mem_map* c_mm, unsigned int* m_size, int factor){
      *m_size *= factor;
      struct addr_str_pair* tmp = malloc(sizeof(struct addr_str_pair)*(*m_size));
      memcpy(tmp, c_mm->cp_mmap, sizeof(struct addr_str_pair)*c_mm->size);
      c_mm->cp_mmap = tmp;
}

// bytes parameter only affects integer mode
void populate_mem_map(struct mem_map* mmap, pid_t pid, int d_rgn, bool use_additional_rgns, bool integers, int bytes){
      mmap->int_mode_bytes = bytes;
      mmap->d_rgn = d_rgn;
      mmap->use_addtnl = use_additional_rgns;
      unsigned int m_size = 0;
      void* vm_l_stack; void* vm_l_heap;
      if(d_rgn == STACK || d_rgn == BOTH){
            vm_l_stack = mmap->mapped_rgn.stack_start_addr;
            m_size = (char*)mmap->mapped_rgn.stack_end_addr-(char*)vm_l_stack;
      }
      if(d_rgn == HEAP || d_rgn == BOTH){
            vm_l_heap = mmap->mapped_rgn.heap_start_addr;
            m_size += ((char*)mmap->mapped_rgn.heap_end_addr-(char*)vm_l_heap);
      }
      if(use_additional_rgns){
            for(int i = 0; i < mmap->mapped_rgn.n_remaining; ++i){
                  m_size += (char*)mmap->mapped_rgn.remaining_addr[i].end-(char*)mmap->mapped_rgn.remaining_addr[i].start;
            }
      }
      mmap->size = 0;
      if(integers){
            m_size /= bytes;
            mmap->mmap = malloc(sizeof(struct addr_int_pair)*m_size);
      }
      else {
            m_size /= 10;
            // can safely assume that not every memory location stores its own string
            // TODO: dynamically reallocate this
            mmap->cp_mmap = malloc(sizeof(struct addr_str_pair)*m_size);
      }
      //TODO: initialize small and resize dynamically
      long buf_s = 0;
      if(integers){
            if(d_rgn == STACK || d_rgn == BOTH){
                  BYTE* ints_in_stack = read_bytes_from_pid_mem(pid, bytes, vm_l_stack, mmap->mapped_rgn.stack_end_addr);
                  for(; vm_l_stack != mmap->mapped_rgn.stack_end_addr; vm_l_stack = (void*)(((char*)vm_l_stack)+bytes)){
                        mmap->mmap[mmap->size].addr = vm_l_stack; mmap->mmap[mmap->size].value = 0;
                        memcpy(&(mmap->mmap[mmap->size++].value), &(ints_in_stack[buf_s]), bytes);
                        buf_s += bytes;
                  }
                  free(ints_in_stack);
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  buf_s = 0;
                  BYTE* ints_in_heap = read_bytes_from_pid_mem(pid, bytes, vm_l_heap, mmap->mapped_rgn.heap_end_addr);
                  for(; vm_l_heap != mmap->mapped_rgn.heap_end_addr; vm_l_heap = (void*)(((char*)vm_l_heap)+bytes)){
                        mmap->mmap[mmap->size].addr = vm_l_heap; mmap->mmap[mmap->size].value = 0;
                        memcpy(&(mmap->mmap[mmap->size++].value), &(ints_in_heap[buf_s]), bytes);
                        buf_s += bytes;
                  }
                  free(ints_in_heap);
            }
            if(use_additional_rgns){
                  for(int i = 0; i < mmap->mapped_rgn.n_remaining; ++i){
                        BYTE* ints_in_addtnl = read_bytes_from_pid_mem(pid, bytes, mmap->mapped_rgn.remaining_addr[i].start,
                                                                                  mmap->mapped_rgn.remaining_addr[i].end);
                        buf_s = 0;
                        for(void* vm_l = mmap->mapped_rgn.remaining_addr[i].start;
                                  vm_l != mmap->mapped_rgn.remaining_addr[i].end;
                                  vm_l = (void*)(((char*)vm_l)+bytes)){
                              mmap->mmap[mmap->size].addr = vm_l; mmap->mmap[mmap->size].value = 0;  
                              memcpy(&(mmap->mmap[mmap->size++].value), &(ints_in_addtnl[buf_s]), bytes);
                              buf_s += bytes;
                        }
                        free(ints_in_addtnl);
                  }
            }
      }
      else{ // !integers
            // TODO: try to populate directly from BYTE* with pointers
            // m_size may be too small
            int len;
            if(d_rgn == STACK || d_rgn == BOTH){
                  // TODO: check for emptiness in strings before adding them
                  char* chars_in_stack = (char*)read_bytes_from_pid_mem(pid, 1, vm_l_stack, mmap->mapped_rgn.stack_end_addr);
                  void* current_addr = vm_l_stack;
                  int n_items = (char*)mmap->mapped_rgn.stack_end_addr-(char*)vm_l_stack;
                  len = 0;
                  for(int i = 0; i < n_items; ++i){
                        if(chars_in_stack[i] > 0 && chars_in_stack[i] < 127){
                              // resize by a factor of 2 - this is repeated in heap and additional region sections
                              if(mmap->size == m_size)resize_str_mmap(mmap, &m_size, 2);
                              len = strlen(chars_in_stack+i);
                              mmap->cp_mmap[mmap->size].addr = current_addr;
                              mmap->cp_mmap[mmap->size].value = malloc(sizeof(char)*(len+1));
                              mmap->cp_mmap[mmap->size].value[len] = '\0';
                              memcpy(mmap->cp_mmap[mmap->size++].value, chars_in_stack+i, len);
                              i += len;
                              current_addr = (void*)(((char*)current_addr)+len);
                        }
                        current_addr = (void*)(((char*)current_addr)+1);
                  }
                  free(chars_in_stack);
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  char* chars_in_heap = (char*)read_bytes_from_pid_mem(pid, 1, vm_l_heap, mmap->mapped_rgn.heap_end_addr);
                  void* current_addr = vm_l_heap;
                  int n_items = (char*)mmap->mapped_rgn.heap_end_addr-(char*)vm_l_heap;
                  len = 0;
                  for(int i = 0; i < n_items; ++i){
                        if(chars_in_heap[i] > 0 && chars_in_heap[i] < 127){
                              if(mmap->size == m_size)resize_str_mmap(mmap, &m_size, 2);
                              len = strlen(chars_in_heap+i);
                              mmap->cp_mmap[mmap->size].addr = current_addr;
                              mmap->cp_mmap[mmap->size].value = malloc(sizeof(char)*(len+1));
                              mmap->cp_mmap[mmap->size].value[len] = '\0';
                              memcpy(mmap->cp_mmap[mmap->size++].value, chars_in_heap+i, len);
                              i += len;
                              current_addr = (void*)(((char*)current_addr)+len);
                        }
                        current_addr = (void*)(((char*)current_addr)+1);
                  }
                  free(chars_in_heap);
            }
            if(use_additional_rgns){
                  int n_items;
                  char* chars_in_addtnl;
                  void* current_addr;
                  len = 0;
                  for(int i = 0; i < mmap->mapped_rgn.n_remaining; ++i){
                        chars_in_addtnl = (char*)read_bytes_from_pid_mem(pid, 1, mmap->mapped_rgn.remaining_addr[i].start, mmap->mapped_rgn.remaining_addr[i].end);
                        current_addr = mmap->mapped_rgn.remaining_addr[i].start;
                        n_items = (char*)mmap->mapped_rgn.remaining_addr[i].end-(char*)mmap->mapped_rgn.remaining_addr[i].start;
                        for(int j = 0; j < n_items; ++j){
                              if(chars_in_addtnl[j] > 0 && chars_in_addtnl[j] < 127){
                                    if(mmap->size == m_size)resize_str_mmap(mmap, &m_size, 2);
                                    len = strlen(chars_in_addtnl+j);
                                    mmap->cp_mmap[mmap->size].addr = current_addr;
                                    mmap->cp_mmap[mmap->size].value = malloc(sizeof(char)*(len+1));
                                    mmap->cp_mmap[mmap->size].value[len] = '\0';
                                    memcpy(mmap->cp_mmap[mmap->size++].value, chars_in_addtnl+j, len);
                                    j += len;
                                    current_addr = (void*)(((char*)current_addr)+len);
                              }
                              current_addr = (void*)(((char*)current_addr)+1);
                        }
                        free(chars_in_addtnl);
                  }
            }
            if(mmap->size < m_size){
                  struct addr_str_pair* tmp_cp_mmap = malloc(sizeof(struct addr_str_pair)*mmap->size);
                  memcpy(tmp_cp_mmap, mmap->cp_mmap, sizeof(struct addr_str_pair)*mmap->size);
                  free(mmap->cp_mmap);
                  mmap->cp_mmap = tmp_cp_mmap;
            }
      }
}

void update_mem_map(struct mem_map* mem, bool integers){
      if(!integers || LOW_MEM || mem->size < RELOAD_CUTOFF){
            if(integers){
                  for(unsigned long i = 0; i < mem->size; ++i){
                        mem->mmap[i].value = read_single_val_from_pid_mem(mem->pid, mem->int_mode_bytes, mem->mmap[i].addr);
                  }
            }
            else{
                  int len;
                  for(unsigned long i = 0; i < mem->size; ++i){
                        len = strlen(mem->cp_mmap[i].value);
                        read_bytes_from_pid_mem_dir(mem->cp_mmap[i].value, mem->pid, 1, mem->cp_mmap[i].addr, (void*)((char*)mem->cp_mmap[i].addr+len));
                  }
            }
      }
      // TODO: implement similar optimization for strings, possibly create a separate parse_strings_from_bytes(BYTE* bytes) function with the code from populate_mem_map for strings to make this simpler
      else{
            struct mem_map tmp_mm;
            tmp_mm.mapped_rgn = mem->mapped_rgn;
            populate_mem_map(&tmp_mm, mem->pid, mem->d_rgn, mem->use_addtnl, integers, mem->int_mode_bytes);
            int cc = 0;
            for(unsigned long i = 0; i < mem->size; ++i){
                  if(mem->mmap[i].addr == tmp_mm.mmap[i].addr){
                        mem->mmap[i].value = tmp_mm.mmap[i].value;
                  }
                  else{
                        ++cc;
                        mem->mmap[i].value = read_single_val_from_pid_mem(mem->pid, mem->int_mode_bytes, mem->mmap[i].addr);
                        ++i;
                  }
            }
            free_mem_map(&tmp_mm, integers);
      }
}

void narrow_mem_map_int(struct mem_map* mem, int match){
      unsigned long initial = mem->size;
      for(unsigned long i = 0; i < mem->size; ++i){
            if(mem->mmap[i].value != match){
                  mem->mmap[i--] = mem->mmap[--mem->size];
                  /*
                   *  // essentially, 
                   *  mmap[i] = mmap[mem->size-1];
                   *  --mem->size;
                   *  --i;
                   *
                   */
            }
      }
      if(mem->size == 0){
            free_mem_map(mem, true);
            return;
      }
      if(mem->size < initial){
            struct addr_int_pair* tmp_mmap = malloc(sizeof(struct addr_int_pair)*mem->size);
            memcpy(tmp_mmap, mem->mmap, sizeof(struct addr_int_pair)*mem->size);
            free(mem->mmap);
            mem->mmap = tmp_mmap;
      }
}

void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact){
      unsigned long initial = mem->size;
      for(unsigned long i = 0; i < mem->size; ++i){
            if(exact){
                  if(mem->cp_mmap[i].addr == 0 || strcmp(mem->cp_mmap[i].value, match) != 0){
                        free(mem->cp_mmap[i].value);
                        mem->cp_mmap[i--] = mem->cp_mmap[--mem->size];
                  }
            }
            else{
                  if(mem->cp_mmap[i].addr == 0 || !strstr(mem->cp_mmap[i].value, match)){
                        free(mem->cp_mmap[i].value);
                        --mem->size;
                        if(mem->size == 0)break;
                        mem->cp_mmap[i--] = mem->cp_mmap[mem->size];
                  }
            }
      }
      // to make sure not to try to reallocate empty mmap with resize
      if(mem->size == 0){
            free_mem_map(mem, false);
            return;
      }
      if(mem->size < initial){
            struct addr_str_pair* tmp_cp_mmap = malloc(sizeof(struct addr_str_pair)*mem->size);
            memcpy(tmp_cp_mmap, mem->cp_mmap, sizeof(struct addr_str_pair)*mem->size);
            free(mem->cp_mmap);
            mem->cp_mmap = tmp_cp_mmap;
      }
}
