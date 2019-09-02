#include "vmem_access.h"

#include <string.h>
#include <sys/uio.h>

// with less than RELOAD_CUTOFF values, it's faster to do individual reads for integers when updating mem_map
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

void free_mem_map(struct mem_map* mem){
      if(mem->integers)free(mem->i_mmap);
      else{
            if(!mem->blk->in_place){
                  for(unsigned int i = 0; i < mem->size; ++i)
                        free(mem->s_mmap[i].value);
            }
            else free_blkstr(mem->blk);
            free(mem->s_mmap); 
            mem->s_mmap = NULL;
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

// if mem == NULL, a pointer to a malloc'd mem_map struct is returned
struct mem_map* mem_map_init(struct mem_map* mem, pid_t pid, bool unmarked_additional){
      if(!mem)mem = malloc(sizeof(struct mem_map));
      mem->size = mem->i_size = mem->s_size = 0;
      mem->mapped_rgn = get_vmem_locations(pid, unmarked_additional);
      mem->low_mem = false;
      mem->force_block_str = true;
      // to silence valgrind errors, overwritten by populate_mem_map's set_mode_mem_map call
      mem->integers = true;
      return mem;
}

// stores mem->i_size or mem->s_size for later use, sets up mem->size
bool set_mode_mem_map(struct mem_map* mem, bool integers){
      if(mem->integers == integers)return false;
      if(mem->integers)mem->i_size = mem->size;
      else mem->s_size = mem->size;
      mem->integers = integers;
      mem->size = integers ? mem->i_size : mem->s_size;
      return true;
}

void init_i_map(struct i_mmap_map* imm, int n_bux, int n_entries){
      imm->n_bux = n_bux;
      imm->i_buckets = malloc(sizeof(struct addr_int_pair*)*n_bux);
      imm->bucket_ref = calloc(sizeof(int), n_bux);

      /* to limit the amount of memory used, we'll assume that most buckets will have less than n_entries/n_bux */
      /* this, of course, implies that we'll need to resize buckets in some cases */
      int bucket_sz = 0.7*(n_entries/n_bux);

      for(int i = 0; i < n_bux; ++i){
            imm->i_buckets[i] = calloc(sizeof(struct addr_int_pair), (bucket_sz+1));
            imm->i_buckets[i][bucket_sz].addr = (void*)0x6969;
      }

      imm->in_place = 1;
}

void free_i_map(struct i_mmap_map* imm){
      for(int i = 0; i < imm->n_bux; ++i)
            free(imm->i_buckets[i]);
      free(imm->i_buckets);
      free(imm->bucket_ref);
}

void insert_i_map(struct i_mmap_map* imm, void* addr, int value){
      int ind = value % imm->n_bux;
      ind = (ind < 0) ? -1*ind : ind;
      int ind_prog = imm->bucket_ref[ind];
      if(imm->i_buckets[ind][ind_prog].addr == (void*)0x6969){
            struct addr_int_pair* tmp_ip = malloc(sizeof(struct addr_int_pair)*((ind_prog*2)+1));
            tmp_ip[(ind_prog*2)].addr = (void*)0x6969;
            memcpy(tmp_ip, imm->i_buckets[ind], sizeof(struct addr_int_pair)*ind_prog);
            free(imm->i_buckets[ind]);
            imm->i_buckets[ind] = tmp_ip;
      }
      imm->i_buckets[ind][ind_prog].addr = addr;
      imm->i_buckets[ind][imm->bucket_ref[ind]++].value = value;
}

void populate_mem_map(struct mem_map* mem, int d_rgn, bool use_additional_rgns, bool integers, int bytes){
      // store size, toggle mem->integers in case mem is being repopulated with a different mode
      set_mode_mem_map(mem, integers);
      mem->size = 0;
      mem->int_mode_bytes = bytes;
      mem->d_rgn = d_rgn;
      mem->use_addtnl = use_additional_rgns;
      unsigned int m_size = 0;
      if(d_rgn == STACK || d_rgn == BOTH)m_size = (char*)mem->mapped_rgn.stack.end-(char*)mem->mapped_rgn.stack.start;
      if(d_rgn == HEAP || d_rgn == BOTH)m_size += (char*)mem->mapped_rgn.heap.end-(char*)mem->mapped_rgn.heap.start;
      if(use_additional_rgns)
            for(int i = 0; i < mem->mapped_rgn.n_remaining; ++i)
                  m_size += (char*)mem->mapped_rgn.remaining_addr[i].end-(char*)mem->mapped_rgn.remaining_addr[i].start;
      unsigned long buf_s = 0;
      // TODO: fix integer mode when int_mode_bytes > 4
      if(integers){
            /* populate_mem_map will always return an int mmap with hashed integers */
            m_size /= bytes;

            init_i_map(&mem->i_mmap_hash, 50, m_size);

            mem->i_mmap = malloc(sizeof(struct addr_int_pair)*m_size);
            int tmp_val;

            if(d_rgn == STACK || d_rgn == BOTH){
                  BYTE* ints_in_stack = read_bytes_from_pid_mem(mem->mapped_rgn.pid, bytes, mem->mapped_rgn.stack.start, mem->mapped_rgn.stack.end);
                  // TODO: in place math optimization
                  /* this iteration is very slow
                   * population can be sped up if only an initial mem addr is stored and ((char*)m_addr)+index_chk*bytes is computed on the fly
                   * easy to reason about, we need to find m_addr of value at index index_chk, each value takes up n_bytes
                   *
                   * this will decrease both space and time complexity significantly - we no longer need to iterate over each n_bytes of of mem
                   * and we no longer need to store sizeof(void*) for every single addr_{int,str}_pair
                   */
                  for(char* sp = mem->mapped_rgn.stack.start; sp != mem->mapped_rgn.stack.end; sp += bytes){
                        memcpy(&tmp_val, ints_in_stack+buf_s, bytes);
                        buf_s += bytes;
                        insert_i_map(&mem->i_mmap_hash, (void*)sp, tmp_val);
                        ++mem->size;
                  }
                  free(ints_in_stack);
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  buf_s = 0;
                  BYTE* ints_in_heap = read_bytes_from_pid_mem(mem->mapped_rgn.pid, bytes, mem->mapped_rgn.heap.start, mem->mapped_rgn.heap.end);
                  for(char* hp = mem->mapped_rgn.heap.start; hp != mem->mapped_rgn.heap.end; hp += bytes){
                        memcpy(&tmp_val, ints_in_heap+buf_s, bytes);
                        buf_s += bytes;
                        insert_i_map(&mem->i_mmap_hash, (void*)hp, tmp_val);
                        ++mem->size;
                  }
                  free(ints_in_heap);
            }
            if(use_additional_rgns){
                  for(int i = 0; i < mem->mapped_rgn.n_remaining; ++i){
                        BYTE* ints_in_addtnl = read_bytes_from_pid_mem(mem->mapped_rgn.pid, bytes, mem->mapped_rgn.remaining_addr[i].start,
                                                                                   mem->mapped_rgn.remaining_addr[i].end);
                        buf_s = 0;
                        for(char* ap = mem->mapped_rgn.remaining_addr[i].start;
                                  ap != mem->mapped_rgn.remaining_addr[i].end; ap += bytes){

                              memcpy(&tmp_val, ints_in_addtnl+buf_s, bytes); 
                              buf_s += bytes;
                              insert_i_map(&mem->i_mmap_hash, (void*)ap, tmp_val);
                              ++mem->size;
                        }
                        free(ints_in_addtnl);
                  }
            }
      }
      else{ // !integers
            m_size /= 10;
            mem->s_mmap = malloc(sizeof(struct addr_str_pair)*m_size);
            /* populate_mem_map will always result in in_place strs
             * even in case of low_mem, strings are malloc'd in narrow_mem_map_str */
            // these must be initialized to NULL to avoid free errors in free_mem_map
            mem->blk = malloc(sizeof(struct str_blk));
            mem->blk->stack = mem->blk->heap = NULL;
            mem->blk->addtnl = NULL;
            mem->blk->in_place = true;
            mem->blk->n_ad = 0;
            int len;
            if(d_rgn == STACK || d_rgn == BOTH){
                  char* chars_in_stack = (char*)read_bytes_from_pid_mem(mem->mapped_rgn.pid, 1, mem->mapped_rgn.stack.start, mem->mapped_rgn.stack.end);
                  mem->blk->stack = chars_in_stack;
                  int n_addr = (char*)mem->mapped_rgn.stack.end-(char*)mem->mapped_rgn.stack.start;
                  for(char* cis_p = chars_in_stack; cis_p != chars_in_stack+n_addr; ++cis_p){
                        if(*cis_p > 0 && *cis_p < 127){
                              // resize by a factor of 2 - this is repeated in heap and additional region sections
                              if(mem->size == m_size)resize_str_mmap(mem, &m_size, 2);
                              len = strlen(cis_p);
                              mem->s_mmap[mem->size].addr = (void*)((char*)mem->mapped_rgn.stack.start+(cis_p-chars_in_stack));
                              mem->s_mmap[mem->size++].value = cis_p;
                              cis_p += len;
                        }
                  }
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  char* chars_in_heap = (char*)read_bytes_from_pid_mem(mem->mapped_rgn.pid, 1, mem->mapped_rgn.heap.start, mem->mapped_rgn.heap.end);
                  mem->blk->heap = chars_in_heap;
                  int n_addr = (char*)mem->mapped_rgn.heap.end-(char*)mem->mapped_rgn.heap.start;
                  len = 0;
                  for(char* cih_p = chars_in_heap; cih_p != chars_in_heap+n_addr; ++cih_p){
                        if(*cih_p > 0 && *cih_p < 127){
                              if(mem->size == m_size)resize_str_mmap(mem, &m_size, 2);
                              len = strlen(cih_p);
                              mem->s_mmap[mem->size].addr = (void*)((char*)mem->mapped_rgn.heap.start+(cih_p-chars_in_heap));
                              mem->s_mmap[mem->size++].value = cih_p;
                              cih_p += len;
                        }
                  }
            }
            if(use_additional_rgns){
                  int n_addr;
                  char* chars_in_addtnl;
                  mem->blk->n_ad = mem->mapped_rgn.n_remaining;
                  mem->blk->addtnl = malloc(sizeof(char*)*mem->blk->n_ad);
                  len = 0;
                  for(int i = 0; i < mem->mapped_rgn.n_remaining; ++i){
                        chars_in_addtnl = (char*)read_bytes_from_pid_mem(mem->mapped_rgn.pid, 1, mem->mapped_rgn.remaining_addr[i].start, mem->mapped_rgn.remaining_addr[i].end);
                        mem->blk->addtnl[i] = chars_in_addtnl;
                        n_addr = (char*)mem->mapped_rgn.remaining_addr[i].end-(char*)mem->mapped_rgn.remaining_addr[i].start;
                        for(char* cia_p = chars_in_addtnl; cia_p != chars_in_addtnl+n_addr; ++cia_p){
                              if(*cia_p > 0 && *cia_p < 127){
                                    if(mem->size == m_size)resize_str_mmap(mem, &m_size, 2);
                                    len = strlen(cia_p);
                                    mem->s_mmap[mem->size].addr = (void*)((char*)mem->mapped_rgn.remaining_addr[i].start+(cia_p-chars_in_addtnl));
                                    mem->s_mmap[mem->size++].value = cia_p;
                                    cia_p += len;
                              }
                        }
                  }
            }
            if(mem->size < m_size){
                  struct addr_str_pair* tmp_s_mmap = malloc(sizeof(struct addr_str_pair)*mem->size);
                  memcpy(tmp_s_mmap, mem->s_mmap, sizeof(struct addr_str_pair)*mem->size);
                  free(mem->s_mmap);
                  mem->s_mmap = tmp_s_mmap;
            }
      }
}

void update_mem_map(struct mem_map* mem){
      if(mem->size == 0)return;
      // TODO: should string update optimization always be used? it's much faster
      if(mem->low_mem || (!mem->integers && (!mem->blk->in_place || mem->size < RELOAD_CUTOFF/10000)) || (mem->integers && mem->size < RELOAD_CUTOFF)){
            if(mem->integers){
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
            if(mem->integers){
                  struct mem_map tmp_mm;
                  tmp_mm.mapped_rgn = mem->mapped_rgn;
                  populate_mem_map(&tmp_mm, mem->d_rgn, mem->use_addtnl, mem->integers, mem->int_mode_bytes);
                  for(unsigned int i = 0; i < mem->size; ++i){
                        if(mem->i_mmap[i].addr == tmp_mm.i_mmap[i].addr)mem->i_mmap[i].value = tmp_mm.i_mmap[i].value;
                        else{
                              mem->i_mmap[i].value = read_single_val_from_pid_mem(mem->mapped_rgn.pid, mem->int_mode_bytes, mem->i_mmap[i].addr);
                              ++i;
                        }
                  }
                  free_mem_map(&tmp_mm);
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

void narrow_mem_map_int_nopt(struct mem_map* mem, int match){
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
            free_mem_map(mem);
            return;
      }
      if(mem->size < initial){
            struct addr_int_pair* tmp_mmap = malloc(sizeof(struct addr_int_pair)*mem->size);
            memcpy(tmp_mmap, mem->i_mmap, sizeof(struct addr_int_pair)*mem->size);
            free(mem->i_mmap);
            mem->i_mmap = tmp_mmap;
      }
}

/* regularize_i_mmap_hash() converts a one dimensional i_mmap_hash that has been narrowed
 * into a standar i_mmap
 */
_Bool regularize_i_mmap_hash(struct mem_map* mem){
      /*this should only be called when there is just one bucket left*/
      if(!mem->i_mmap_hash.in_place || mem->i_mmap_hash.n_bux != 1)return 0;
      mem->i_mmap = *mem->i_mmap_hash.i_buckets;
      mem->i_size = mem->size = *mem->i_mmap_hash.bucket_ref;

      mem->i_mmap_hash.in_place = 0;
      return 1;
}

void narrow_mem_map_int(struct mem_map* mem, int match){
      if(!mem->i_mmap_hash.in_place)narrow_mem_map_int_nopt(mem, match);
      else{
            int ind = match % mem->i_mmap_hash.n_bux;
            ind = (ind < 0) ? -1*ind : ind;

            for(int i = 0; i < mem->i_mmap_hash.n_bux; ++i){
                  if(i != ind){
                        free(mem->i_mmap_hash.i_buckets[i]);
                        /* TODO: should we just hide values instead of freeing them? */
                        mem->i_mmap_hash.bucket_ref[i] = 0;
                  }
            }
            

            struct addr_int_pair** tmp_aip = malloc(sizeof(struct addr_int_pair*));
            tmp_aip[0] = malloc(sizeof(struct addr_int_pair)*(mem->i_mmap_hash.bucket_ref[ind])+1);
            tmp_aip[0][mem->i_mmap_hash.bucket_ref[ind]].addr = (void*)0x6969;

            /* TODO:
             * we could ignore the problem of finding matches and just regularize
             * the matche can be found using narrow_mem_map_int_nopt()
             * just simply:
             *       throw out buckets that we don't need
             *       keep one
             *       regularize
             *       _nopt()
            */


            unsigned int adj_size = 0;
            for(int i = 0; i < mem->i_mmap_hash.bucket_ref[ind]; ++i)
                  if(mem->i_mmap_hash.i_buckets[ind][i].value == match){
                        tmp_aip[0][adj_size].value = mem->i_mmap_hash.i_buckets[ind][i].value;
                        tmp_aip[0][adj_size++].addr = mem->i_mmap_hash.i_buckets[ind][i].addr;
                  }
      
            

            /* TODO: this should leverage init* */
             mem->i_mmap_hash.n_bux = 1;
             free(mem->i_mmap_hash.bucket_ref);
             mem->i_mmap_hash.bucket_ref = malloc(sizeof(int));
             *mem->i_mmap_hash.bucket_ref = adj_size;
 
             free(mem->i_mmap_hash.i_buckets);
             mem->i_mmap_hash.i_buckets = tmp_aip;

            /*free_i_map(&mem->i_mmap_hash);*/
            /*init_i_map(&mem->i_mmap_hash, 1, );*/

            regularize_i_mmap_hash(mem);
      }
}

/* this function finds the last match of needle in haystack, useful for exact_e in narrow_mem_map_str
 * TODO: is there a better way to do this?
 */
char* adj_m(char* haystack, const char* needle, bool exact_s){
      if(exact_s)return haystack;
      char* c = haystack;
      char* p;
      while((p = strstr(c, needle)))c = ++p;
      return c-1;
}

void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact_s, bool exact_e){
      // if match string is a malformed string
      if(!match || !*match)return;
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
            free_mem_map(mem);
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
                        free_mem_map(mem);
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
bool print_locks(struct lock_container* lc){
      if(lc->n == lc->n_removed)return false;
      unsigned int r_i = 0;
      for(unsigned int i = 0; i < lc->n; ++i){
            if(!lc->locks[i].active)continue;
            // strings
            if(!lc->locks[i].integers)printf("(%i) %p: \"%s\"", r_i, *lc->locks[i].m_addr, *lc->locks[i].s_val);
            else printf("(%i) %p: %i", r_i, *lc->locks[i].m_addr, *lc->locks[i].i_val);
            if(lc->locks[i].rng)fputs(" (multiple locks)", stdout); 
            puts("");
            ++r_i;
      }
      return true;
}

/* if keep_first, the s_val in the rm_s of lc will not be freed
   every other string in s_val that requires freeing will still be freed
   M_ADDR, I_VAL AND S_VAL WILL NOT BE FREED BY REMOVE_LOCK UNLESS FREE_OP IS SET as follows: 0: nothing, 1: just maddr, 2: just i/sval, 3: m and i/s */
   // TODO: add free_op documentation
// remove_lock returns long as to not truncate unsigned int - must be signed to return -1 when lock not found
long remove_lock(struct lock_container* lc, unsigned int rm_s, bool keep_first, char free_op){
      if(lc->n == lc->n_removed || rm_s >= lc->n-lc->n_removed)return -1;
      unsigned int r_i = 0;
      pthread_mutex_lock(&lc->mut_lck);
      long ret = -1;
      for(unsigned int i = 0; i < lc->n; ++i){
            if(!lc->locks[i].active)continue;
            if(r_i == rm_s){
                  ++lc->n_removed;
                  lc->locks[i].active = false;
                  if(!lc->locks[i].integers){
                        unsigned int u_lim = (lc->locks[i].mul_val) ? lc->locks[i].n_addr : 1;
                        for(unsigned int f = keep_first; f < u_lim; ++f)free(lc->locks[i].s_val[f]);
                  }
                  ret = i;
                  break;
            }
            ++r_i;
      }
      pthread_mutex_unlock(&lc->mut_lck);
      if(lc->n == lc->n_removed)pthread_join(lc->thread, NULL);
      if(free_op%2 != 0)free(lc->locks[ret].m_addr);
      if(free_op > 1){
            if(lc->locks[ret].integers)free(lc->locks[ret].i_val);
            else if(!keep_first)free(lc->locks[ret].s_val);
      }
      return ret;
}

// returns number of locks removed before freeing
unsigned int free_locks(struct lock_container* lc, char free_op){
      unsigned int i = 0;
      for(i = 0; remove_lock(lc, 0, false, free_op) != -1; ++i);
      free(lc->locks);
      pthread_mutex_destroy(&lc->mut_lck);
      return i;
}

// if lc == NULL, a pointer to a malloc'd lock_container struct is returned
struct lock_container* lock_container_init(struct lock_container* lc, unsigned int initial_sz){
      if(lc == NULL)lc = malloc(sizeof(struct lock_container));
      lc->n = lc->n_removed = 0;
      lc->cap = initial_sz;
      lc->locks = malloc(sizeof(struct lock_entry)*lc->cap);
      pthread_mutex_t mlck;
      pthread_mutex_init(&mlck, NULL);
      lc->mut_lck = mlck;
      return lc;
}

unsigned long lock_th(struct lock_container* lc){
      unsigned long iter = 0;
      while(lc->n != lc->n_removed){
            ++iter;
            usleep(1000);
            pthread_mutex_lock(&lc->mut_lck);
            for(unsigned int i = 0; i < lc->n; ++i){
                  if(lc->locks[i].active){
                        for(unsigned int j = 0; j < lc->locks[i].n_addr; ++j){
                              if(lc->locks[i].integers){
                                    if(lc->locks[i].mul_val)write_int_to_pid_mem(lc->locks[i].pid, lc->locks[i].m_addr[j], lc->locks[i].i_val[j]);
                                    else write_int_to_pid_mem(lc->locks[i].pid, lc->locks[i].m_addr[j], *lc->locks[i].i_val);
                              }
                              else{
                                    if(lc->locks[i].mul_val)write_str_to_pid_mem(lc->locks[i].pid, lc->locks[i].m_addr[j], lc->locks[i].s_val[j]);
                                    else write_str_to_pid_mem(lc->locks[i].pid, lc->locks[i].m_addr[j], *lc->locks[i].s_val);
                              }
                        }
                  }
            }
            pthread_mutex_unlock(&lc->mut_lck);
      }
      return iter;
}

void* lock_pthread(void* lc){
      return (void*)lock_th((struct lock_container*)lc);
}

// TODO add int_mode_bytes functionality
// returns true if thread was created, false if thread not created
bool create_lock(struct lock_container* lc, pid_t pid, void** addr, int* i_val, char** s_val, unsigned int n_addr, bool mul_val, bool integers){
      pthread_t lock_th;
      pthread_mutex_lock(&lc->mut_lck);
      if(lc->n == lc->cap){
            lc->cap *= 2;
            struct lock_entry* tmp_l = malloc(sizeof(struct lock_entry)*lc->cap);
            memcpy(tmp_l, lc->locks, sizeof(struct lock_entry)*lc->n);
            free(lc->locks);
            lc->locks = tmp_l;
      }
      if(integers)lc->locks[lc->n].s_val = NULL;
      lc->locks[lc->n].active = true;
      lc->locks[lc->n].pid = pid;
      lc->locks[lc->n].integers = integers;
      lc->locks[lc->n].mul_val = mul_val;
      lc->locks[lc->n].n_addr = n_addr;
      lc->locks[lc->n].s_val = s_val;
      lc->locks[lc->n].i_val = i_val;
      lc->locks[lc->n].rng = n_addr != 1;
      lc->locks[lc->n].m_addr = addr;
      ++lc->n;
      pthread_mutex_unlock(&lc->mut_lck);
      // if we have one lock after adding one - if we just added the first lock
      if(lc->n-1 == lc->n_removed){
            pthread_create(&lock_th, NULL, &lock_pthread, lc);
            lc->thread = lock_th;
            return true;
      }
      return false;
}
