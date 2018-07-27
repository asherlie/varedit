#include "vmem_access.h"

#include <string.h>
#include <sys/uio.h>

#include <signal.h> // kill()
#include <sys/wait.h> // wait()

// with less than 1000000 values, it's faster to do individual reads for integers when updating mem_map
#define RELOAD_CUTOFF 1000000

void free_blkstr(struct str_blk* blk){
      if(!blk->in_place)return;
      if(blk->stack)free(blk->stack);
      if(blk->heap)free(blk->heap);
      for(unsigned char i = 0; i < blk->n_ad; ++i)
            if(blk->addtnl[i])free(blk->addtnl[i]);
      if(blk->addtnl)free(blk->addtnl);
      free(blk);
}

void free_mem_map(struct mem_map* mmap, bool integers){
      if(integers)free(mmap->i_mmap);
      else{
            if(!mmap->blk->in_place){
                  for(unsigned int i = 0; i < mmap->size; ++i)
                        free(mmap->s_mmap[i].value);
            }
            else free_blkstr(mmap->blk);
            free(mmap->s_mmap); 
            mmap->s_mmap = NULL;
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
 * min_strlen can safely be set to 0 and last_avail to NULL with an efficiency tradeoff
 * THIS FUNCTION SHOULD ONLY BE USED WHEN STRING SIZE IS UNKNOWN
 */ 
char* read_str_from_mem_range_slow_dir(char* dest, pid_t pid, void* mb_start, int min_strlen, void* last_avail){
      char tmp;
      int str_sz = min_strlen+5;
      char* ret = dest;
      if(!dest)ret = malloc(sizeof(char)*str_sz+1);
      int ret_p = 0;
      if(min_strlen > 0)read_bytes_from_pid_mem_dir(ret, pid, min_strlen, mb_start, NULL);
      for(char* i = (char*)mb_start+min_strlen; i != last_avail; ++i){
            tmp = read_single_val_from_pid_mem(pid, 1, (void*)i);
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
      // don't use read_bytes_from_pid_mem to read from current process
      BYTE* bytes = (src_pid == getpid()) ? (BYTE*)src : read_bytes_from_pid_mem(src_pid, n_bytes, src, NULL);
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
      memcpy(tmp, c_mm->s_mmap, sizeof(struct addr_str_pair)*c_mm->size);
      free(c_mm->s_mmap);
      c_mm->s_mmap = tmp;
}

// if mem == NULL a new struct mem_map is malloc'd
struct mem_map* mem_map_init(struct mem_map* mem, pid_t pid, bool unmarked_additional){
      if(!mem)mem = malloc(sizeof(struct mem_map));
      mem->size = 0;
      mem->mapped_rgn = get_vmem_locations(pid, unmarked_additional);
      mem->low_mem = false;
      mem->force_block_str = true;
      return mem;
}

void populate_mem_map(struct mem_map* mmap, int d_rgn, bool use_additional_rgns, bool integers, int bytes){
      // TODO: possibly remove this because mem_map_init makes it redundant
      mmap->size = 0;
      mmap->int_mode_bytes = bytes;
      mmap->d_rgn = d_rgn;
      mmap->use_addtnl = use_additional_rgns;
      unsigned int m_size = 0;
      if(d_rgn == STACK || d_rgn == BOTH)m_size = (char*)mmap->mapped_rgn.stack.end-(char*)mmap->mapped_rgn.stack.start;
      if(d_rgn == HEAP || d_rgn == BOTH)m_size += (char*)mmap->mapped_rgn.heap.end-(char*)mmap->mapped_rgn.heap.start;
      if(use_additional_rgns){
            for(int i = 0; i < mmap->mapped_rgn.n_remaining; ++i)
                  m_size += (char*)mmap->mapped_rgn.remaining_addr[i].end-(char*)mmap->mapped_rgn.remaining_addr[i].start;
      }
      unsigned long buf_s = 0;
      // TODO: fix integer mode when int_mode_bytes > 4
      if(integers){
            m_size /= bytes;
            mmap->i_mmap = malloc(sizeof(struct addr_int_pair)*m_size);
            if(d_rgn == STACK || d_rgn == BOTH){
                  BYTE* ints_in_stack = read_bytes_from_pid_mem(mmap->mapped_rgn.pid, bytes, mmap->mapped_rgn.stack.start, mmap->mapped_rgn.stack.end);
                  for(char* sp = mmap->mapped_rgn.stack.start; sp != mmap->mapped_rgn.stack.end; sp += bytes){
                        mmap->i_mmap[mmap->size].addr = (void*)sp; mmap->i_mmap[mmap->size].value = 0;
                        memcpy(&(mmap->i_mmap[mmap->size++].value), ints_in_stack+buf_s, bytes);
                        buf_s += bytes;
                  }
                  free(ints_in_stack);
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  buf_s = 0;
                  BYTE* ints_in_heap = read_bytes_from_pid_mem(mmap->mapped_rgn.pid, bytes, mmap->mapped_rgn.heap.start, mmap->mapped_rgn.heap.end);
                  for(char* hp = mmap->mapped_rgn.heap.start; hp != mmap->mapped_rgn.heap.end; hp += bytes){
                        mmap->i_mmap[mmap->size].addr = (void*)hp; mmap->i_mmap[mmap->size].value = 0;
                        memcpy(&(mmap->i_mmap[mmap->size++].value), ints_in_heap+buf_s, bytes);
                        buf_s += bytes;
                  }
                  free(ints_in_heap);
            }
            if(use_additional_rgns){
                  for(int i = 0; i < mmap->mapped_rgn.n_remaining; ++i){
                        BYTE* ints_in_addtnl = read_bytes_from_pid_mem(mmap->mapped_rgn.pid, bytes, mmap->mapped_rgn.remaining_addr[i].start,
                                                                                   mmap->mapped_rgn.remaining_addr[i].end);
                        buf_s = 0;
                        for(char* ap = mmap->mapped_rgn.remaining_addr[i].start;
                                  ap != mmap->mapped_rgn.remaining_addr[i].end; ap += bytes){
                              mmap->i_mmap[mmap->size].addr = (void*)ap; mmap->i_mmap[mmap->size].value = 0;  
                              memcpy(&(mmap->i_mmap[mmap->size++].value), ints_in_addtnl+buf_s, bytes);
                              buf_s += bytes;
                        }
                        free(ints_in_addtnl);
                  }
            }
      }
      else{ // !integers
            m_size /= 10;
            mmap->s_mmap = malloc(sizeof(struct addr_str_pair)*m_size);
            /* populate_mem_map will always result in in_place strs
             * even in case of low_mem, strings are malloc'd in narrow_mem_map_str */
            // these must be initialized to NULL to avoid free errors in free_mem_map
            mmap->blk = malloc(sizeof(struct str_blk));
            mmap->blk->stack = mmap->blk->heap = NULL;
            mmap->blk->addtnl = NULL;
            mmap->blk->in_place = true;
            mmap->blk->n_ad = 0;
            int len;
            if(d_rgn == STACK || d_rgn == BOTH){
                  char* chars_in_stack = (char*)read_bytes_from_pid_mem(mmap->mapped_rgn.pid, 1, mmap->mapped_rgn.stack.start, mmap->mapped_rgn.stack.end);
                  mmap->blk->stack = chars_in_stack;
                  int n_addr = (char*)mmap->mapped_rgn.stack.end-(char*)mmap->mapped_rgn.stack.start;
                  for(char* cis_p = chars_in_stack; cis_p != chars_in_stack+n_addr; ++cis_p){
                        if(*cis_p > 0 && *cis_p < 127){
                              // resize by a factor of 2 - this is repeated in heap and additional region sections
                              if(mmap->size == m_size)resize_str_mmap(mmap, &m_size, 2);
                              len = strlen(cis_p);
                              mmap->s_mmap[mmap->size].addr = (void*)((char*)mmap->mapped_rgn.stack.start+(cis_p-chars_in_stack));
                              mmap->s_mmap[mmap->size++].value = cis_p;
                              cis_p += len;
                        }
                  }
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  char* chars_in_heap = (char*)read_bytes_from_pid_mem(mmap->mapped_rgn.pid, 1, mmap->mapped_rgn.heap.start, mmap->mapped_rgn.heap.end);
                  mmap->blk->heap = chars_in_heap;
                  int n_addr = (char*)mmap->mapped_rgn.heap.end-(char*)mmap->mapped_rgn.heap.start;
                  len = 0;
                  for(char* cih_p = chars_in_heap; cih_p != chars_in_heap+n_addr; ++cih_p){
                        if(*cih_p > 0 && *cih_p < 127){
                              if(mmap->size == m_size)resize_str_mmap(mmap, &m_size, 2);
                              len = strlen(cih_p);
                              mmap->s_mmap[mmap->size].addr = (void*)((char*)mmap->mapped_rgn.heap.start+(cih_p-chars_in_heap));
                              mmap->s_mmap[mmap->size++].value = cih_p;
                              cih_p += len;
                        }
                  }
            }
            if(use_additional_rgns){
                  int n_addr;
                  char* chars_in_addtnl;
                  mmap->blk->n_ad = mmap->mapped_rgn.n_remaining;
                  mmap->blk->addtnl = malloc(sizeof(char*)*mmap->blk->n_ad);
                  len = 0;
                  for(int i = 0; i < mmap->mapped_rgn.n_remaining; ++i){
                        chars_in_addtnl = (char*)read_bytes_from_pid_mem(mmap->mapped_rgn.pid, 1, mmap->mapped_rgn.remaining_addr[i].start, mmap->mapped_rgn.remaining_addr[i].end);
                        mmap->blk->addtnl[i] = chars_in_addtnl;
                        n_addr = (char*)mmap->mapped_rgn.remaining_addr[i].end-(char*)mmap->mapped_rgn.remaining_addr[i].start;
                        for(char* cia_p = chars_in_addtnl; cia_p != chars_in_addtnl+n_addr; ++cia_p){
                              if(*cia_p > 0 && *cia_p < 127){
                                    if(mmap->size == m_size)resize_str_mmap(mmap, &m_size, 2);
                                    len = strlen(cia_p);
                                    mmap->s_mmap[mmap->size].addr = (void*)((char*)mmap->mapped_rgn.remaining_addr[i].start+(cia_p-chars_in_addtnl));
                                    mmap->s_mmap[mmap->size++].value = cia_p;
                                    cia_p += len;
                              }
                        }
                  }
            }
            if(mmap->size < m_size){
                  struct addr_str_pair* tmp_s_mmap = malloc(sizeof(struct addr_str_pair)*mmap->size);
                  memcpy(tmp_s_mmap, mmap->s_mmap, sizeof(struct addr_str_pair)*mmap->size);
                  free(mmap->s_mmap);
                  mmap->s_mmap = tmp_s_mmap;
            }
      }
}

void update_mem_map(struct mem_map* mem, bool integers){
      if(mem->size == 0)return;
      // TODO: should string update optimization always be used? it's much faster
      if(mem->low_mem || (!integers && (!mem->blk->in_place || mem->size < RELOAD_CUTOFF/10000)) || (integers && mem->size < RELOAD_CUTOFF)){
            if(integers){
                  for(unsigned int i = 0; i < mem->size; ++i)
                        mem->i_mmap[i].value = read_single_val_from_pid_mem(mem->mapped_rgn.pid, mem->int_mode_bytes, mem->i_mmap[i].addr);
            }
            else{
                  for(unsigned int i = 0; i < mem->size; ++i)
                        // this method works for both blkstr mode and individually alloc'd strings
                        read_bytes_from_pid_mem_dir(mem->s_mmap[i].value, mem->mapped_rgn.pid, strlen(mem->s_mmap[i].value), mem->s_mmap[i].addr, NULL);
            }
      }
      else{ // faster but more memory intensive update methods
            if(integers){
                  struct mem_map tmp_mm;
                  tmp_mm.mapped_rgn = mem->mapped_rgn;
                  populate_mem_map(&tmp_mm, mem->d_rgn, mem->use_addtnl, integers, mem->int_mode_bytes);
                  for(unsigned int i = 0; i < mem->size; ++i){
                        if(mem->i_mmap[i].addr == tmp_mm.i_mmap[i].addr)mem->i_mmap[i].value = tmp_mm.i_mmap[i].value;
                        else{
                              mem->i_mmap[i].value = read_single_val_from_pid_mem(mem->mapped_rgn.pid, mem->int_mode_bytes, mem->i_mmap[i].addr);
                              ++i;
                        }
                  }
                  free_mem_map(&tmp_mm, integers);
            }
            else{
                  if(mem->blk->stack)
                        read_bytes_from_pid_mem_dir(mem->blk->stack, mem->mapped_rgn.pid, 1, mem->mapped_rgn.stack.start, mem->mapped_rgn.stack.end);
                  if(mem->blk->heap)
                        read_bytes_from_pid_mem_dir(mem->blk->heap, mem->mapped_rgn.pid, 1, mem->mapped_rgn.heap.start, mem->mapped_rgn.heap.end);
                  for(unsigned char i = 0; i < mem->blk->n_ad; ++i){
                        if(mem->blk->addtnl[i])
                        read_bytes_from_pid_mem_dir(mem->blk->addtnl[i], mem->mapped_rgn.pid, 1, mem->mapped_rgn.remaining_addr[i].start, mem->mapped_rgn.remaining_addr[i].end);
                  }
            }
      }
}

void narrow_mem_map_int(struct mem_map* mem, int match){
      unsigned int initial = mem->size;
      for(unsigned int i = 0; i < mem->size; ++i){
            if(mem->i_mmap[i].value != match){
                  mem->i_mmap[i--] = mem->i_mmap[--mem->size];
                  /*
                   *  // more simply,
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
            memcpy(tmp_mmap, mem->i_mmap, sizeof(struct addr_int_pair)*mem->size);
            free(mem->i_mmap);
            mem->i_mmap = tmp_mmap;
      }
}

/* this function finds the last match of needle in haystack, useful for exact_e in narrow_mem_map_str
 * TODO: is there a better way to do this?
 */
char* adj_m(char* haystack, const char* needle, bool exact_s){
      if(exact_s)return haystack;
      char* c = haystack;
      char* p;
      while((p = strstr(c, needle))){
            ++p;
            c = p;
      }
      return c-1;
}

void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact_s, bool exact_e){
      unsigned int initial = mem->size;
      int mlen = strlen(match);
      for(unsigned int i = 0; i < mem->size; ++i){
            char* s = strstr(mem->s_mmap[i].value, match);
            if(!s || (exact_s && s != mem->s_mmap[i].value) || (exact_e && adj_m(mem->s_mmap[i].value, match, exact_s)[mlen] != '\0')){
                  if(!mem->blk->in_place)free(mem->s_mmap[i].value);
                  --mem->size;
                  if(mem->size == 0)break;
                  mem->s_mmap[i--] = mem->s_mmap[mem->size];
            }
      }
      // to make sure not to try to reallocate empty mmap with resize
      if(mem->size == 0){
            free_mem_map(mem, false);
            return;
      }
      if(mem->size < initial){
            struct addr_str_pair* tmp_s_mmap = malloc(sizeof(struct addr_str_pair)*mem->size);
            /* if we have low memory, or have narrowed sufficiently, it's worthwhile to indiviually allocate strings
             * this is trivial because we are realloc'ing anyway for a resize and need to copy entries */
            // TODO: test usage of RELOAD_CUTOFF for this condition 
            if(mem->blk->in_place && (mem->low_mem || mem->size < (initial/1000) || mem->size < 1000)){
                  // if we have very few values, it's likely that they are not from a diverse group of regions
                  // we'll try to free any unused region blocks
                  if(mem->force_block_str){
                        int rgn;
                        bool s = false, h = false;
                        // +1 in case of n_ad == 0
                        bool a[mem->blk->n_ad+1];
                        memset(a, 0, mem->mapped_rgn.n_remaining);
                        for(unsigned int i = 0; i < mem->size; ++i){
                              which_rgn(mem->mapped_rgn, mem->s_mmap[i].addr, &rgn);
                              switch(rgn){
                                    case STACK: s = true; break;
                                    case HEAP:  h = true; break;
                                    default:    if(rgn >= 2)a[rgn-2] = true;
                              }
                              tmp_s_mmap[i] = mem->s_mmap[i];
                        }
                        if(!s && mem->blk->stack){
                              free(mem->blk->stack); mem->blk->stack = NULL;
                        }
                        if(!h && mem->blk->heap){
                              free(mem->blk->heap); mem->blk->heap = NULL;
                        }
                        for(unsigned char i = 0; i < mem->blk->n_ad; ++i){
                              if(!a[i] && mem->blk->addtnl[i]){
                                    free(mem->blk->addtnl[i]); mem->blk->addtnl[i] = NULL;
                              }
                        }
                  }
                  else{
                        for(unsigned int i = 0; i < mem->size; ++i){
                              tmp_s_mmap[i].addr = mem->s_mmap[i].addr;
                              tmp_s_mmap[i].value = strdup(mem->s_mmap[i].value);
                        }
                        free_mem_map(mem, false);
                        mem->blk->in_place = false;
                  }
            }
            else
                  memcpy(tmp_s_mmap, mem->s_mmap, sizeof(struct addr_str_pair)*mem->size);
            if(mem->s_mmap)free(mem->s_mmap);
            mem->s_mmap = tmp_s_mmap;
      }
}

// TODO: possibly move all lock functions and struct definitions to separate files mem_lock.{c,h}
bool print_locks(struct lock_container* lc, bool integers){
      if(lc->n-lc->n_removed == 0)return false;
      unsigned int r_i = 0;
      for(unsigned int i = 0; i < lc->n; ++i){
            if(lc->locks[i].m_addr == NULL)continue;
            if(integers)printf("(%i) %p: %i", r_i, lc->locks[i].m_addr, lc->locks[i].i_value);
            else printf("(%i) %p: \"%s\"", r_i, lc->locks[i].m_addr, lc->locks[i].s_value);
            if(lc->locks[i].rng)fputs(" (multiple locks)", stdout); 
            puts("");
            ++r_i;
      }
      return true;
}

/* if keep_first, the s_value in the rm_s of lc will not be freed
   every other string in to_free that requires freeing will still be freed */
int remove_lock(struct lock_container* lc, unsigned int rm_s, bool keep_first){
      if(lc->n-lc->n_removed == 0 || rm_s >= lc->n-lc->n_removed)return -1;
      unsigned int r_i = 0;
      for(unsigned int i = 0; i < lc->n; ++i){
            if(lc->locks[i].m_addr == NULL)continue;
            if(r_i == rm_s){
                  if(lc->locks[i].to_free != NULL){
                        // will only be > 0 if !integers
                        for(int f = keep_first; f < lc->locks[i].n_to_free; ++f)
                              free(((char**)lc->locks[i].to_free)[f]);
                        free(lc->locks[i].to_free);
                        lc->locks[i].to_free = NULL;
                  }
                  kill(lc->locks[i].pid, SIGKILL);
                  wait(NULL);
                  ++lc->n_removed;
                  // setting to null as to not print it later
                  lc->locks[i].m_addr = NULL;
                  return i;
            }
            ++r_i;
      }
      return -1;
}

// returns number of locks removed before freeing
unsigned int free_locks(struct lock_container* lc){
      unsigned int i = 0;
      for(i = 0; remove_lock(lc, 0, false) != -1; ++i);
      free(lc->locks);
      return i;
}

// if lc == NULL, a new lock_container struct is malloc'd
struct lock_container* lock_container_init(struct lock_container* lc, unsigned int initial_sz){
      if(lc == NULL)lc = malloc(sizeof(struct lock_container));
      lc->n = lc->n_removed = 0;
      lc->cap = initial_sz;
      lc->locks = malloc(sizeof(struct lock_entry)*lc->cap);
      return lc;
}

// TODO add int_mode_bytes functionality
// if f_o_r is not null, it'll be freed on removal
int create_lock(struct lock_container* lc, pid_t pid, void** addr, int* i_val, char** s_val, unsigned int n_addr, bool mul_val, bool integers, void* f_o_r){
      pid_t fpid = fork();
      if(fpid == 0){
            while(1){
                  usleep(1000);
                  for(unsigned int i = 0; i < n_addr; ++i){
                        if(integers){
                              // TODO: use write_bytes_to_pid_mem and build the BYTE[] outside of loop to optimize
                              if(mul_val)write_int_to_pid_mem(pid, addr[i], i_val[i]);
                              else write_int_to_pid_mem(pid, addr[i], *i_val);
                        }
                        else{
                              if(mul_val)write_str_to_pid_mem(pid, addr[i], s_val[i]);
                              else write_str_to_pid_mem(pid, addr[i], *s_val);
                        }
                  }
            }
      }
      if(lc->n == lc->cap){
            lc->cap *= 2;
            struct lock_entry* tmp_l = malloc(sizeof(struct lock_entry)*lc->cap);
            memcpy(tmp_l, lc->locks, sizeof(struct lock_entry)*lc->n);
            free(lc->locks);
            lc->locks = tmp_l;
      }
      if(!integers)lc->locks[lc->n].s_value = *s_val;
      else lc->locks[lc->n].i_value = *i_val;
      lc->locks[lc->n].pid = fpid;
      lc->locks[lc->n].rng = n_addr != 1;
      lc->locks[lc->n].m_addr = *addr;
      lc->locks[lc->n].to_free = f_o_r;
      lc->locks[lc->n].n_to_free = 0;
      if(!integers){
            if(mul_val)lc->locks[lc->n].n_to_free = n_addr;
            else lc->locks[lc->n].n_to_free = 1;
      }
      ++lc->n;
      return fpid;
}
