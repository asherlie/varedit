#include "vmem_access.h"
#include <sys/uio.h>
#include <limits.h> // for IOV_MAX

#define STACK 0
#define HEAP 1
#define BOTH 2

int* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e){
      int sz_rgn;
      if(vm_e == nullptr)sz_rgn = bytes;
      else sz_rgn = (char*)vm_e-(char*)vm_s;
      int n_items = sz_rgn/bytes;
      struct iovec* local = new struct iovec[n_items];
      struct iovec remote[1];
      int* buf = new int[n_items];
      int byte_c = 0;
      for(int i = 0; i < n_items; ++i){
            local[i].iov_base = &(buf[i]);
            local[i].iov_len = bytes;
            byte_c+=bytes;
      }
      remote[0].iov_base = vm_s;
      remote[0].iov_len = sz_rgn;
      int c = 0;
      for(int i = 0; i < (n_items/IOV_MAX)+1; ++i){
            process_vm_readv(pid, local+c, IOV_MAX, remote, 1, 0);
            remote[0].iov_base = (void*)(((char*)remote[0].iov_base)+IOV_MAX*bytes);
            c+=IOV_MAX;
      }
      delete[] local;
      return buf;
}

int read_single_int_from_pid_mem(pid_t pid, void* vm){
      int buf;
      struct iovec local[1];
      struct iovec remote[1];
      local[0].iov_base = &buf;
      local[0].iov_len = 4;
      remote[0].iov_len = 4;
      remote[0].iov_base = vm;
      process_vm_readv(pid, local, 1, remote, 1, 0);
      return buf;
}

char read_single_char_from_pid_mem(pid_t pid, void* vm){
      char buf;
      struct iovec local[1];
      struct iovec remote[1];
      local[0].iov_base = &buf;
      local[0].iov_len = 1;
      remote[0].iov_base = vm;
      remote[0].iov_len = 1;
      process_vm_readv(pid, local, 1, remote, 1, 0);
      return buf;
}

std::string read_str_from_mem_block_slow(pid_t pid, void* mb_start, void* mb_end=nullptr){
// TODO: maybe change this to read in blocks of 10/20 chars and then parse. would be faster
// regardless, should probably switch to read_bytes_from_pid_mem implementation
      char tmp;
      std::string ret = "";
      for(void* i = mb_start; i != mb_end; i = (void*)(((char*)i)+1)){
            tmp = read_single_char_from_pid_mem(pid, i);
            if(!(tmp > 0 && tmp < 127)){
                  return ret;
            }
            ret += tmp;
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

bool write_str_to_pid_mem(pid_t pid, void* vm, std::string str){
      int s_c = 0;
      unsigned int written = 0; 
      for(void* i = vm; i != (void*)(((char*)vm)+str.size()); i = (void*)((char*)i+1)){
            written += write_int_to_pid_mem(pid, i, str[s_c++]);
      }
      //write_int_to_pid_mem(pid, (void*)(((char*)vm)+str.size()), '\0');
      return written == str.size();
}

void populate_mem_map(mem_map &mmap, pid_t pid, int d_rgn=STACK, bool use_additional_rgns=true, bool integers=true){
      long m_size = 0;
      void* vm_l_stack; void* vm_l_end_stack; void* vm_l_heap; void* vm_l_end_heap;
      if(d_rgn == STACK || d_rgn == BOTH){
            vm_l_stack = mmap.mapped_rgn.stack_start_addr;
            vm_l_end_stack = mmap.mapped_rgn.stack_end_addr;
            m_size = (char*)vm_l_end_stack-(char*)vm_l_stack;
      }
      if(d_rgn == HEAP || d_rgn == BOTH){
            vm_l_heap = mmap.mapped_rgn.heap_start_addr;
            vm_l_end_heap = mmap.mapped_rgn.heap_end_addr;
            m_size += ((char*)vm_l_end_heap-(char*)vm_l_heap);
      }
      if(use_additional_rgns){
            for(int i = 0; i < mmap.mapped_rgn.n_remaining; ++i){
                  m_size += (char*)mmap.mapped_rgn.remaining_addr[i].second-(char*)mmap.mapped_rgn.remaining_addr[i].first;
            }
      }
      mmap.size = 0;
      if(integers)mmap.mmap = new std::pair<void*, int>[m_size]; //TODO: try using [m_size/4]
      else mmap.cp_mmap = new std::pair<void*, std::string>[m_size];
      //TODO: find smarter way to allocate string mmap memory, this assumes that each memory location stores an individual string
      long c = 0;
      if(integers){
            mmap.size = m_size;
            if(d_rgn == STACK || d_rgn == BOTH){
                  int* ints_in_stack = read_bytes_from_pid_mem(pid, 4, vm_l_stack, vm_l_end_stack);
                  for(int d = 0; vm_l_stack != vm_l_end_stack; vm_l_stack = (void*)(((char*)vm_l_stack)+4)){ // +4 for ints
                        mmap.mmap[c++] = std::make_pair(vm_l_stack, ints_in_stack[d++]);
                  }
                  delete[] ints_in_stack;
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  int* ints_in_heap = read_bytes_from_pid_mem(pid, 4, vm_l_heap, vm_l_end_heap);
                  for(int d = 0; vm_l_heap != vm_l_end_heap; vm_l_heap = (void*)(((char*)vm_l_heap)+4)){
                        mmap.mmap[c++] = std::make_pair(vm_l_heap, ints_in_heap[d++]);
                  }
                  delete[] ints_in_heap;

            }
            if(use_additional_rgns){
                  for(int i = 0; i < mmap.mapped_rgn.n_remaining; ++i){
                        int* ints_in_adtnl = read_bytes_from_pid_mem(pid, 4, mmap.mapped_rgn.remaining_addr[i].first, 
                                                                             mmap.mapped_rgn.remaining_addr[i].second);
                        int d = 0;
                        for(void* vm_l = mmap.mapped_rgn.remaining_addr[i].first;
                                  vm_l != mmap.mapped_rgn.remaining_addr[i].second;
                                  vm_l = (void*)(((char*)vm_l)+4)){
                              mmap.mmap[c++] = std::make_pair(vm_l, ints_in_adtnl[d++]);
                        }
                        delete[] ints_in_adtnl;
                  }
            }
      }
      else{
            // when searching for end of string, all permutations of string show up
            std::string tmp;
            bool in_str = false;
            if(d_rgn == STACK || d_rgn == BOTH){
                  tmp = "";
                  int* chars_in_stack = read_bytes_from_pid_mem(pid, 1, vm_l_stack, vm_l_end_stack);
                  void* current_addr = vm_l_stack; void* str_st_addr;
                  for(int i = 0; i < (char*)vm_l_end_stack-(char*)vm_l_stack; ++i){
                        if(chars_in_stack[i] > 0 && chars_in_stack[i] < 127){
                              if(!in_str)str_st_addr = current_addr; // first char of a string
                              in_str = true;
                              tmp += (char)chars_in_stack[i];
                        }
                        else if(in_str){
                              in_str = false;
                              mmap.cp_mmap[c++] = std::make_pair(str_st_addr, tmp);
                              ++mmap.size;
                              tmp = "";
                        }
                        current_addr = (void*)(((char*)current_addr)+1);
                  }
                  delete[] chars_in_stack;
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  in_str = false;
                  tmp = "";
                  int* chars_in_heap = read_bytes_from_pid_mem(pid, 1, vm_l_heap, vm_l_end_heap);
                  void* current_addr = vm_l_heap; void* str_st_addr;
                  for(int i = 0; i < (char*)vm_l_end_heap-(char*)vm_l_heap; ++i){
                        if(chars_in_heap[i] > 0 && chars_in_heap[i] < 127){
                              if(!in_str)str_st_addr = current_addr;
                              in_str = true;
                              tmp += (char)chars_in_heap[i];
                        }
                        else if(in_str){
                              in_str = false;
                              mmap.cp_mmap[c++] = std::make_pair(str_st_addr, tmp);
                              ++mmap.size;
                              tmp = "";
                        }
                        current_addr = (void*)(((char*)current_addr)+1);
                  }
                  delete[] chars_in_heap;
            }
            if(use_additional_rgns){
                  in_str = false;
                  tmp = "";
                  int* chars_in_addtnl;
                  void* current_addr; void* str_st_addr;
                  for(int i = 0; i < mmap.mapped_rgn.n_remaining; ++i){
                        chars_in_addtnl = read_bytes_from_pid_mem(pid, 1, mmap.mapped_rgn.remaining_addr[i].first, mmap.mapped_rgn.remaining_addr[i].second);
                        current_addr = mmap.mapped_rgn.remaining_addr[i].first;
                        for(int j = 0; j < (char*)mmap.mapped_rgn.remaining_addr[i].second-(char*)mmap.mapped_rgn.remaining_addr[i].first; ++j){
                              if(chars_in_addtnl[j] > 0 && chars_in_addtnl[j] < 127){
                                    if(!in_str)str_st_addr = current_addr;
                                    in_str = true;
                                    tmp += (char)chars_in_addtnl[j];
                              }
                              else if(in_str){
                                    in_str = false;
                                    mmap.cp_mmap[c++] = std::make_pair(str_st_addr, tmp);
                                    ++mmap.size;
                                    tmp = "";
                              }
                        }
                        delete[] chars_in_addtnl;
                  }
            }
      }
}

void update_mem_map(mem_map &mem, bool integers=true){
      // TODO: check for consecutive mem rgns, to leverage the faster read_bytes_from_pid_mem
      if(integers){
            for(int i = 0; i < mem.size; ++i){
                  mem.mmap[i].second = read_single_int_from_pid_mem(mem.pid, mem.mmap[i].first);
            }
      }
      else{
            for(int i = 0; i < mem.size; ++i){
                  mem.cp_mmap[i].second = read_str_from_mem_block_slow(mem.pid, mem.cp_mmap[i].first);
            }
      }
}

void narrow_mem_map_int(mem_map &mem, int match, bool use_match){ // if !use_match this can be used to delete empty pairs
      std::string match_str = std::to_string(match);
      for(int i = 0; i < mem.size; ++i){
            // std::pair<void*, int> is initialized to <0, 0> - erase these pairs
            if(mem.mmap[i].first == 0 || (use_match && std::to_string(mem.mmap[i].second) != match_str)){ // exact
                  mem.mmap[i--] = mem.mmap[--mem.size];
                  /*
                   *  // essentially, 
                   *  mmap[i] = mmap[mem.size-1];
                   *  --mem.size;
                   *  --i;
                   *
                   */
            }
      }
      std::pair<void*, int>* tmp_mmap = new std::pair<void*, int>[mem.size];
      std::copy(mem.mmap, mem.mmap+mem.size, tmp_mmap);
      delete[] mem.mmap;
      mem.mmap = tmp_mmap;
}

void narrow_mem_map_str(mem_map &mem, std::string match, bool exact=true){
      for(int i = 0; i < mem.size; ++i){
            if(exact){
                  if(mem.cp_mmap[i].first == 0 || mem.cp_mmap[i].second != match){
                        mem.cp_mmap[i--] = mem.cp_mmap[--mem.size];
                  }
            }
            else{
                  if(mem.cp_mmap[i].first == 0 || mem.cp_mmap[i].second.find(match) == std::string::npos){
                        --mem.size;
                        if(mem.size == 0)break;
                        mem.cp_mmap[i--] = mem.cp_mmap[mem.size];
                  }
            }
      }
      std::pair<void*, std::string>* tmp_cp_mmap = new std::pair<void*, std::string>[mem.size];
      std::copy(mem.cp_mmap, mem.cp_mmap+mem.size, tmp_cp_mmap);
      delete[] mem.cp_mmap;
      mem.cp_mmap = tmp_cp_mmap;
}
