#include "vmem_access.h"
#include <string.h>
#include <sys/uio.h>
#include <limits.h> // for IOV_MAX

#define STACK 0
#define HEAP 1
#define BOTH 2

void free_mem_map(struct mem_map* mmap, bool integers){
      if(mmap->mapped_rgn.n_remaining > 0)free(mmap->mapped_rgn.remaining_addr);
      if(integers)free(mmap->mmap);
      else{ // TODO do i need to free all strings
            for(int i = 0; i < mmap->size; ++i){
                  free(mmap->cp_mmap[i].second);
            }
            free(mmap->cp_mmap); 
      }
}

int* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e){
      int sz_rgn;
      if(vm_e == NULL)sz_rgn = bytes;
      else sz_rgn = (char*)vm_e-(char*)vm_s;
      int n_items = sz_rgn/bytes;
      struct iovec* local = (struct iovec*)malloc(sizeof(struct iovec)*n_items);
      struct iovec remote[1];
      int* buf = (int*)malloc(sizeof(int)*n_items);
      int byte_c = 0;
      for(int i = 0; i < n_items; ++i){
            local[i].iov_base = &(buf[i]);
            local[i].iov_len = bytes;
            byte_c+=bytes;
      }
      remote[0].iov_base = vm_s;
      remote[0].iov_len = sz_rgn;
      int c = 0;
      for(int i = 0; i < (n_items/__IOV_MAX)+1; ++i){
            process_vm_readv(pid, local+c, __IOV_MAX, remote, 1, 0);
            remote[0].iov_base = (void*)(((char*)remote[0].iov_base)+__IOV_MAX*bytes);
            c+=__IOV_MAX;
      }
      free(local);
      return buf;
}

int read_single_val_from_pid_mem(pid_t pid, int bytes, void* vm){
      int ret;
      struct iovec local[1];
      struct iovec remote[1];
      local->iov_base = &ret;
      local->iov_len = bytes;
      remote->iov_base = vm;
      remote->iov_len = bytes;
      process_vm_readv(pid, local, 1, remote, 1, 0);
      return ret;
}

char* read_str_from_mem_block_slow(pid_t pid, void* mb_start, void* mb_end){
// TODO: maybe change this to read in blocks of 10/20 chars and then parse. would be faster
// regardless, should probably switch to read_bytes_from_pid_mem implementation
      char tmp;
      char* ret = (char*)malloc(sizeof(char)*100); int ret_p = 0;
      int str_sz = 100;
      for(void* i = mb_start; i != mb_end; i = (void*)(((char*)i)+1)){ 
            tmp = (char)read_single_val_from_pid_mem(pid, 1, i);
            if(!(tmp > 0 && tmp < 127)){
                  ret[ret_p] = '\0';
                  return ret;
            }
            if(ret_p >= str_sz)str_sz += 10;
            char tmp_ret[str_sz-10];
            strcpy(tmp_ret, ret);
            free(ret);
            ret = (char*)malloc(sizeof(char*)*str_sz);
            strcpy(ret, tmp_ret);
            ret[ret_p++] = tmp;
      }
      return ret;
}

bool write_int_to_pid_mem(pid_t pid, void* vm, int value){
      int buff_sz = 4; // sizeof int
      int buf[buff_sz];
      buf[0] = value;
      struct iovec local[1];
      struct iovec remote[1];
      local[0].iov_base = buf;
      local[0].iov_len = buff_sz;
      remote[0].iov_len = buff_sz;
      remote[0].iov_base = vm;
      return (buff_sz == process_vm_writev((pid_t)pid, local, 1, remote, 1, 0));
}

bool write_str_to_pid_mem(pid_t pid, void* vm, const char* str){
      int s_c = 0;
      unsigned int written = 0; 
      for(void* i = vm; i != (void*)(((char*)vm)+strlen(str)); i = (void*)((char*)i+1)){
            written += write_int_to_pid_mem(pid, i, str[s_c++]);
      }
      return written == strlen(str);
}

void populate_mem_map(struct mem_map* mmap, pid_t pid, int d_rgn, bool use_additional_rgns, bool integers){
      long m_size = 0;
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
                  m_size += (char*)mmap->mapped_rgn.remaining_addr[i].second-(char*)mmap->mapped_rgn.remaining_addr[i].first;
            }
      }
      mmap->size = 0;
      if(integers){
            m_size /= 4;
            mmap->mmap = (struct addr_int_pair*)malloc(sizeof(struct addr_int_pair)*m_size);
      }
      else mmap->cp_mmap = (struct addr_str_pair*)malloc(sizeof(struct addr_str_pair)*m_size);
      //TODO: find smarter way to allocate string mmap memory, this assumes that each memory location stores an individual string
      long c = 0;
      if(integers){
            mmap->size = m_size;
            if(d_rgn == STACK || d_rgn == BOTH){
                  int* ints_in_stack = read_bytes_from_pid_mem(pid, 4, vm_l_stack, mmap->mapped_rgn.stack_end_addr);
                  for(int d = 0; vm_l_stack != mmap->mapped_rgn.stack_end_addr; vm_l_stack = (void*)(((char*)vm_l_stack)+4)){ // +4 for ints
                        struct addr_int_pair tmp_pair;
                        tmp_pair.first = vm_l_stack; tmp_pair.second = ints_in_stack[d++];
                        mmap->mmap[c++] = tmp_pair;
                  }
                  free(ints_in_stack);
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  int* ints_in_heap = read_bytes_from_pid_mem(pid, 4, vm_l_heap, mmap->mapped_rgn.heap_end_addr);
                  for(int d = 0; vm_l_heap != mmap->mapped_rgn.heap_end_addr; vm_l_heap = (void*)(((char*)vm_l_heap)+4)){
                        struct addr_int_pair tmp_pair;
                        tmp_pair.first = vm_l_heap; tmp_pair.second = ints_in_heap[d++];
                        mmap->mmap[c++] = tmp_pair;
                  }
                  free(ints_in_heap);

            }
            if(use_additional_rgns){
                  for(int i = 0; i < mmap->mapped_rgn.n_remaining; ++i){
                        int* ints_in_adtnl = read_bytes_from_pid_mem(pid, 4, mmap->mapped_rgn.remaining_addr[i].first, 
                                                                             mmap->mapped_rgn.remaining_addr[i].second);
                        int d = 0;
                        for(void* vm_l = mmap->mapped_rgn.remaining_addr[i].first;
                                  vm_l != mmap->mapped_rgn.remaining_addr[i].second;
                                  vm_l = (void*)(((char*)vm_l)+4)){
                              // TODO: might have to malloc these pairs
                              struct addr_int_pair tmp;
                              tmp.first = vm_l; tmp.second = ints_in_adtnl[d++];
                              mmap->mmap[c++] = tmp;
                        }
                        free(ints_in_adtnl);
                  }
            }
      }
      else{
            // when searching for end of string, all permutations of string show up
            //char* tmp = (char*)malloc(sizeof(char)*tmp_size);
            bool in_str = false;
            if(d_rgn == STACK || d_rgn == BOTH){
                  // dont forget to dealloc this after each loop
                  int tmp_size = 22;
                  char* tmp = (char*)malloc(sizeof(char)*tmp_size); int tmp_p = 0;
                  memset(tmp, '\0', sizeof(char)*tmp_size);
                  /*tmp[0] = '\0';*/
                  int* chars_in_stack = read_bytes_from_pid_mem(pid, 1, vm_l_stack, mmap->mapped_rgn.stack_end_addr);
                  void* current_addr = vm_l_stack; void* str_st_addr;
                  for(int i = 0; i < (char*)mmap->mapped_rgn.stack_end_addr-(char*)vm_l_stack; ++i){
                        if(chars_in_stack[i] > 0 && chars_in_stack[i] < 127){
                              if(!in_str)str_st_addr = current_addr; // first char of a string
                              in_str = true;
                              if(tmp_p >= tmp_size-1){
                                    tmp_size += 20;
                                    char* tmp_tmp = (char*)malloc(sizeof(char)*tmp_size);
                                    strcpy(tmp_tmp, tmp);
                                    free(tmp);
                                    tmp = tmp_tmp;
                              }
                              tmp[tmp_p++] = (char)chars_in_stack[i];
                        }
                        else if(in_str){
                              in_str = false;
                              tmp_p = 0;
                              struct addr_str_pair tmp_pair;
                              tmp_pair.first = str_st_addr; tmp_pair.second = tmp;
                              mmap->cp_mmap[c++] = tmp_pair;
                              ++mmap->size;
                              /*tmp[0] = '\0';*/
                              memset(tmp, '\0', sizeof(char)*tmp_size);
                              /* maybe resize string bc it stays big from last iter */
                        }
                        current_addr = (void*)(((char*)current_addr)+1);
                  }
                  free(chars_in_stack);
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  in_str = false;
                  int tmp_size = 22;
                  char* tmp = (char*)malloc(sizeof(char)*tmp_size); int tmp_p = 0;
                  tmp[0] = '\0';
                  int* chars_in_heap = read_bytes_from_pid_mem(pid, 1, vm_l_heap, mmap->mapped_rgn.heap_end_addr);
                  void* current_addr = vm_l_heap; void* str_st_addr;
                  for(int i = 0; i < (char*)mmap->mapped_rgn.heap_end_addr-(char*)vm_l_heap; ++i){
                        if(chars_in_heap[i] > 0 && chars_in_heap[i] < 127){
                              if(!in_str)str_st_addr = current_addr;
                              in_str = true;
                              if(tmp_p >= tmp_size){
                                    tmp_size += 20;
                                    char* tmp_tmp = (char*)malloc(sizeof(char)*tmp_size);
                                    strcpy(tmp_tmp, tmp);
                                    free(tmp);
                                    tmp = tmp_tmp;
                              }
                              tmp[tmp_p++] = (char)chars_in_heap[i];
                        }
                        else if(in_str){
                              in_str = false;
                              struct addr_str_pair tmp_pair;
                              tmp_pair.first = str_st_addr; tmp_pair.second = tmp;
                              mmap->cp_mmap[c++] = tmp_pair;
                              ++mmap->size;
                              tmp[0] = '\0';
                        }
                        current_addr = (void*)(((char*)current_addr)+1);
                  }
                  free(chars_in_heap);
            }
            if(use_additional_rgns){
                  in_str = false;
                  int tmp_size = 22;
                  char* tmp = (char*)malloc(sizeof(char)*tmp_size); int tmp_p = 0;
                  tmp[0] = '\0';
                  int* chars_in_addtnl;
                  void* current_addr; void* str_st_addr;
                  for(int i = 0; i < mmap->mapped_rgn.n_remaining; ++i){
                        chars_in_addtnl = read_bytes_from_pid_mem(pid, 1, mmap->mapped_rgn.remaining_addr[i].first, mmap->mapped_rgn.remaining_addr[i].second);
                        current_addr = mmap->mapped_rgn.remaining_addr[i].first;
                        for(int j = 0; j < (char*)mmap->mapped_rgn.remaining_addr[i].second-(char*)mmap->mapped_rgn.remaining_addr[i].first; ++j){
                              if(chars_in_addtnl[j] > 0 && chars_in_addtnl[j] < 127){
                                    if(!in_str)str_st_addr = current_addr;
                                    in_str = true;
                                    if(tmp_p >= tmp_size){
                                          tmp_size += 20;
                                          char* tmp_tmp = (char*)malloc(sizeof(char)*tmp_size);
                                          strcpy(tmp_tmp, tmp);
                                          free(tmp);
                                          tmp = tmp_tmp;
                                    }
                                    tmp[tmp_p++] = (char)chars_in_addtnl[j];
                              }
                              else if(in_str){
                                    in_str = false;
                                    struct addr_str_pair tmp_pair;
                                    tmp_pair.first = str_st_addr; tmp_pair.second = tmp;
                                    mmap->cp_mmap[c++] = tmp_pair;
                                    ++mmap->size;
                                    tmp[0] = '\0';
                              }
                        }
                        free(chars_in_addtnl);
                  }
            }
      }
}

void update_mem_map(struct mem_map* mem, bool integers){
      if(integers){
            for(int i = 0; i < mem->size; ++i){
                  mem->mmap[i].second = read_single_val_from_pid_mem(mem->pid, 4, mem->mmap[i].first);
            }
      }
      else{
            for(int i = 0; i < mem->size; ++i){
                  mem->cp_mmap[i].second = read_str_from_mem_block_slow(mem->pid, mem->cp_mmap[i].first, NULL);
            }
      }
}

void narrow_mem_map_int(struct mem_map* mem, int match){
      int initial = mem->size;
      for(int i = 0; i < mem->size; ++i){
            if(mem->mmap[i].second != match){
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
      if(mem->size < initial){
            struct addr_int_pair* tmp_mmap = (struct addr_int_pair*)malloc(sizeof(struct addr_int_pair)*mem->size);
            memcpy(tmp_mmap, mem->mmap, sizeof(struct addr_int_pair)*mem->size);
            free(mem->mmap);
            mem->mmap = tmp_mmap;
      }
}

void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact){
      printf("narrowing wrt '%s'", match);
      int initial = mem->size;
      for(int i = 0; i < mem->size; ++i){
            if(exact){
                  if(mem->cp_mmap[i].first == 0 || strcmp(mem->cp_mmap[i].second, match) != 0){
                        mem->cp_mmap[i--] = mem->cp_mmap[--mem->size];
                  }
            }
            else{
                  if(mem->cp_mmap[i].first == 0 || strstr(match, mem->cp_mmap[i].second) == NULL){
                        --mem->size;
                        if(mem->size == 0)break;
                        mem->cp_mmap[i--] = mem->cp_mmap[mem->size];
                  }
            }
      }
      if(mem->size < initial){
            struct addr_str_pair* tmp_cp_mmap = (struct addr_str_pair*)malloc(sizeof(struct addr_str_pair)*mem->size);
            memcpy(tmp_cp_mmap, mem->cp_mmap, sizeof(struct addr_str_pair)*mem->size);
            free(mem->cp_mmap);
            mem->cp_mmap = tmp_cp_mmap;
      }
}
