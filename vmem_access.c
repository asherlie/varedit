#include "vmem_access.h"

#include <string.h>
#include <sys/uio.h>

#define STACK 0
#define HEAP 1
#define BOTH 2

// with less than 1000000 values, it's faster to do individual reads for integers when updating mem_map
#define RELOAD_CUTOFF 1000000
/* 
 * if(LOW_MEM):
 *    update_mem_map optimizations for integers are turned off
 *    strings in cp_mmap are individually allocated as early as possible to save memory
 */
// TODO: document these macros in readme
#define LOW_MEM false
#define FORCE_BLOCK_STR true
/*
 * if(FORCE_BLOCK_STR):
 *    strings in cp_mmap will never be individually allocated 
 */

void free_mem_map(struct mem_map* mmap, bool integers){
      if(integers)free(mmap->mmap);
      else{
            if(!mmap->blk.in_place){
                  for(unsigned int i = 0; i < mmap->size; ++i){
                        free(mmap->cp_mmap[i].value);
                  }
            }
            else{
                  if(mmap->blk.stack)free(mmap->blk.stack);
                  if(mmap->blk.heap)free(mmap->blk.heap);
                  for(unsigned char i = 0; i < mmap->blk.n_ad; ++i){
                        if(mmap->blk.addtnl[i])free(mmap->blk.addtnl[i]);
                  }
                  if(mmap->blk.addtnl)free(mmap->blk.addtnl);
            }
            free(mmap->cp_mmap); 
            mmap->cp_mmap = NULL;
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

/* when dest == NULL, a malloc'd string is returned
 * min_strlen can safely be set to 0 and last_avail can be safely set to NULL
 * this function should only be used when string size is unknown
 */ 
char* read_str_from_mem_range_slow_dir(char* dest, pid_t pid, void* mb_start, int min_strlen, void* last_avail){
      char tmp;
      int str_sz = min_strlen+5;
      char* ret = dest;
      if(!dest)ret = malloc(sizeof(char)*str_sz+1);
      int ret_p = 0;
      if(min_strlen > 0)read_bytes_from_pid_mem_dir(ret, pid, min_strlen, mb_start, NULL);
      for(void* i = mb_start+min_strlen; i != last_avail; ++i){
            tmp = read_single_val_from_pid_mem(pid, 1, i);
            if(!(tmp > 0 && tmp < 127))return ret;
            if(!dest && ret_p == str_sz){
                  str_sz *= 2;
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

char* read_str_from_mem_range_slow(pid_t pid, void* mb_start, void* mb_end){
      return read_str_from_mem_range_slow_dir(NULL, pid, mb_start, 0, mb_end);
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
      free(c_mm->cp_mmap);
      c_mm->cp_mmap = tmp;
}

// bytes parameter only affects integer mode
void populate_mem_map(struct mem_map* mmap, pid_t pid, int d_rgn, bool use_additional_rgns, bool integers, int bytes){
      mmap->int_mode_bytes = bytes;
      mmap->d_rgn = d_rgn;
      mmap->use_addtnl = use_additional_rgns;
      unsigned int m_size = 0;
      if(d_rgn == STACK || d_rgn == BOTH)m_size = (char*)mmap->mapped_rgn.stack_end_addr-(char*)mmap->mapped_rgn.stack_start_addr;
      if(d_rgn == HEAP || d_rgn == BOTH)m_size += (char*)mmap->mapped_rgn.heap_end_addr-(char*)mmap->mapped_rgn.heap_start_addr;
      if(use_additional_rgns){
            for(int i = 0; i < mmap->mapped_rgn.n_remaining; ++i)
                  m_size += (char*)mmap->mapped_rgn.remaining_addr[i].end-(char*)mmap->mapped_rgn.remaining_addr[i].start;
      }
      mmap->size = 0;
      if(integers){
            m_size /= bytes;
            mmap->mmap = malloc(sizeof(struct addr_int_pair)*m_size);
      }
      else {
            m_size /= 10;
            mmap->cp_mmap = malloc(sizeof(struct addr_str_pair)*m_size);
      }
      long buf_s = 0;
      if(integers){
            if(d_rgn == STACK || d_rgn == BOTH){
                  BYTE* ints_in_stack = read_bytes_from_pid_mem(pid, bytes, mmap->mapped_rgn.stack_start_addr, mmap->mapped_rgn.stack_end_addr);
                  for(void* sp = mmap->mapped_rgn.stack_start_addr; sp != mmap->mapped_rgn.stack_end_addr; sp += bytes){
                        mmap->mmap[mmap->size].addr = sp; mmap->mmap[mmap->size].value = 0;
                        memcpy(&(mmap->mmap[mmap->size++].value), &(ints_in_stack[buf_s]), bytes);
                        buf_s += bytes;
                  }
                  free(ints_in_stack);
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  buf_s = 0;
                  BYTE* ints_in_heap = read_bytes_from_pid_mem(pid, bytes, mmap->mapped_rgn.heap_start_addr, mmap->mapped_rgn.heap_end_addr);
                  for(void* hp = mmap->mapped_rgn.heap_start_addr; hp != mmap->mapped_rgn.heap_end_addr; hp += bytes){
                        mmap->mmap[mmap->size].addr = hp; mmap->mmap[mmap->size].value = 0;
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
                        for(void* ap = mmap->mapped_rgn.remaining_addr[i].start;
                                  ap != mmap->mapped_rgn.remaining_addr[i].end; ap += bytes){
                              mmap->mmap[mmap->size].addr = ap; mmap->mmap[mmap->size].value = 0;  
                              memcpy(&(mmap->mmap[mmap->size++].value), &(ints_in_addtnl[buf_s]), bytes);
                              buf_s += bytes;
                        }
                        free(ints_in_addtnl);
                  }
            }
      }
      else{ // !integers
            // populate_mem_map will always result in in_place strs
            // even in case of FORCE_BLOCK_STR, strings are malloc'd in narrow_mem_map_str
            // these must be initialized to NULL to avoid free errors in free_mem_map
            mmap->blk.stack = mmap->blk.heap = NULL;
            mmap->blk.addtnl = NULL;
            mmap->blk.in_place = true;
            mmap->blk.n_ad = 0;
            int len;
            if(d_rgn == STACK || d_rgn == BOTH){
                  char* chars_in_stack = (char*)read_bytes_from_pid_mem(pid, 1, mmap->mapped_rgn.stack_start_addr, mmap->mapped_rgn.stack_end_addr);
                  mmap->blk.stack = chars_in_stack;
                  int n_addr = (char*)mmap->mapped_rgn.stack_end_addr-(char*)mmap->mapped_rgn.stack_start_addr;
                  for(char* cis_p = chars_in_stack; cis_p != chars_in_stack+n_addr; ++cis_p){
                        if(*cis_p > 0 && *cis_p < 127){
                              // resize by a factor of 2 - this is repeated in heap and additional region sections
                              if(mmap->size == m_size)resize_str_mmap(mmap, &m_size, 2);
                              len = strlen(cis_p);
                              mmap->cp_mmap[mmap->size].addr = mmap->mapped_rgn.stack_start_addr+(cis_p-chars_in_stack);
                              mmap->cp_mmap[mmap->size++].value = cis_p;
                              cis_p += len;
                        }
                  }
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  char* chars_in_heap = (char*)read_bytes_from_pid_mem(pid, 1, mmap->mapped_rgn.heap_start_addr, mmap->mapped_rgn.heap_end_addr);
                  mmap->blk.heap = chars_in_heap;
                  int n_addr = (char*)mmap->mapped_rgn.heap_end_addr-(char*)mmap->mapped_rgn.heap_start_addr;
                  len = 0;
                  for(char* cih_p = chars_in_heap; cih_p != chars_in_heap+n_addr; ++cih_p){
                        if(*cih_p > 0 && *cih_p < 127){
                              if(mmap->size == m_size)resize_str_mmap(mmap, &m_size, 2);
                              len = strlen(cih_p);
                              mmap->cp_mmap[mmap->size].addr = mmap->mapped_rgn.heap_start_addr+(cih_p-chars_in_heap);
                              mmap->cp_mmap[mmap->size++].value = cih_p;
                              cih_p += len;
                        }
                  }
            }
            if(use_additional_rgns){
                  int n_addr;
                  char* chars_in_addtnl;
                  mmap->blk.n_ad = mmap->mapped_rgn.n_remaining;
                  mmap->blk.addtnl = malloc(sizeof(char*)*mmap->blk.n_ad);
                  len = 0;
                  for(int i = 0; i < mmap->mapped_rgn.n_remaining; ++i){
                        chars_in_addtnl = (char*)read_bytes_from_pid_mem(pid, 1, mmap->mapped_rgn.remaining_addr[i].start, mmap->mapped_rgn.remaining_addr[i].end);
                        mmap->blk.addtnl[i] = chars_in_addtnl;
                        n_addr = (char*)mmap->mapped_rgn.remaining_addr[i].end-(char*)mmap->mapped_rgn.remaining_addr[i].start;
                        for(char* cia_p = chars_in_addtnl; cia_p != chars_in_addtnl+n_addr; ++cia_p){
                              if(*cia_p > 0 && *cia_p < 127){
                                    if(mmap->size == m_size)resize_str_mmap(mmap, &m_size, 2);
                                    len = strlen(cia_p);
                                    mmap->cp_mmap[mmap->size].addr = mmap->mapped_rgn.remaining_addr[i].start+(cia_p-chars_in_addtnl);
                                    mmap->cp_mmap[mmap->size++].value = cia_p;
                                    cia_p += len;
                              }
                        }
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
                  for(unsigned int i = 0; i < mem->size; ++i){
                        mem->mmap[i].value = read_single_val_from_pid_mem(mem->pid, mem->int_mode_bytes, mem->mmap[i].addr);
                  }
            }
            else{
                  for(unsigned int i = 0; i < mem->size; ++i){
                        // this method works for both str in_place mode and individually alloc'd strings
                        read_bytes_from_pid_mem_dir(mem->cp_mmap[i].value, mem->pid, strlen(mem->cp_mmap[i].value), mem->cp_mmap[i].addr, NULL);
                  }
            }
      }
      // TODO: implement similar optimization for strings, possibly create a separate parse_strings_from_bytes(BYTE* bytes) function with the code from populate_mem_map for strings to make this simpler
      else{
            struct mem_map tmp_mm;
            tmp_mm.mapped_rgn = mem->mapped_rgn;
            populate_mem_map(&tmp_mm, mem->pid, mem->d_rgn, mem->use_addtnl, integers, mem->int_mode_bytes);
            int cc = 0;
            for(unsigned int i = 0; i < mem->size; ++i){
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
      unsigned int initial = mem->size;
      for(unsigned int i = 0; i < mem->size; ++i){
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
      unsigned int initial = mem->size;
      for(unsigned int i = 0; i < mem->size; ++i){
            if(exact){
                  if(mem->cp_mmap[i].addr == 0 || strcmp(mem->cp_mmap[i].value, match) != 0){
                        if(!mem->blk.in_place)free(mem->cp_mmap[i].value);
                        --mem->size;
                        if(mem->size == 0)break;
                        mem->cp_mmap[i--] = mem->cp_mmap[mem->size];
                  }
            }
            else{
                  if(mem->cp_mmap[i].addr == 0 || !strstr(mem->cp_mmap[i].value, match)){
                        if(!mem->blk.in_place)free(mem->cp_mmap[i].value);
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
            // if we have low memory, or have narrowed sufficiently, it's worthwhile to indiviually allocate strings
            // this is trivial because we are realloc'ing anyway and need to copy entries
            // TODO: test usage of RELOAD_CUTOFF for this condition
            if(mem->blk.in_place && (LOW_MEM || mem->size < (initial/1000) || mem->size < 1000)){
                  // if we have very few values, it's likely that they are not from a diverse group of regions
                  // we'll try to free any unnecessary region blocks
                  if(FORCE_BLOCK_STR){
                        int rgn;
                        bool s = false, h = false;
                        // +1 in case of n_ad == 0
                        bool a[mem->blk.n_ad+1];
                        memset(a, 0, mem->mapped_rgn.n_remaining);
                        for(unsigned int i = 0; i < mem->size; ++i){
                              which_rgn(mem->mapped_rgn, mem->cp_mmap[i].addr, &rgn);
                              switch(rgn){
                                    case STACK: s = true; break;
                                    case HEAP:  h = true; break;
                                    default:    if(rgn >= 2)a[rgn-2] = true;
                              }
                              tmp_cp_mmap[i] = mem->cp_mmap[i];
                        }
                        if(!s && mem->blk.stack){
                              free(mem->blk.stack); mem->blk.stack = NULL;
                        }
                        if(!h && mem->blk.heap){
                              free(mem->blk.heap); mem->blk.heap = NULL;
                        }
                        for(unsigned char i = 0; i < mem->blk.n_ad; ++i){
                              if(!a[i] && mem->blk.addtnl[i]){
                                    free(mem->blk.addtnl[i]); mem->blk.addtnl[i] = NULL;
                              }
                        }
                  }
                  else{
                        for(unsigned int i = 0; i < mem->size; ++i){
                              tmp_cp_mmap[i].addr = mem->cp_mmap[i].addr;
                              tmp_cp_mmap[i].value = strdup(mem->cp_mmap[i].value);
                        }
                        free_mem_map(mem, false);
                        mem->blk.in_place = false;
                  }
            }
            else{
                  memcpy(tmp_cp_mmap, mem->cp_mmap, sizeof(struct addr_str_pair)*mem->size);
            }
            if(mem->cp_mmap)free(mem->cp_mmap);
            mem->cp_mmap = tmp_cp_mmap;
      }
}
