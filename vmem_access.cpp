#include "vmem_access.h"
#include <sys/uio.h>

#define STACK 0
#define HEAP 1
#define BOTH 2

int* read_ints_from_pid_mem(pid_t pid, void* vm_s, void* vm_e){
      int sz_rgn = (char*)vm_e-(char*)vm_s;
      struct iovec* local = new struct iovec[sz_rgn];
      struct iovec remote[1];
      int* buf = new int[sz_rgn];
      for(int i = 0; i < sz_rgn; ++i){
            // treating diff regions of one array as separate arrays
            local[i].iov_base = &(buf[i]);
            local[i].iov_len = 4; // int
      }
      remote[0].iov_base = vm_s;
      remote[0].iov_len = sz_rgn;
      process_vm_readv((pid_t)pid, local, 1, remote, 1, 0);
      delete[] local;
      return buf;
}

int read_int_from_pid_mem(pid_t pid, void* vm){
      int buff_sz = 4; // sizeof int
      int buf[buff_sz];
      struct iovec local[1];
      struct iovec remote[1];
      local[0].iov_base = buf;
      local[0].iov_len = buff_sz;
      remote[0].iov_len = buff_sz;
      remote[0].iov_base = vm;
      process_vm_readv((pid_t)pid, local, 1, remote, 1, 0);
      return *buf;
}

char read_char_from_pid_mem(pid_t pid, void* vm){
      int buff_sz = 1;
      char buf[buff_sz];
      struct iovec local[1];
      struct iovec remote[1];
      local[0].iov_base = buf;
      local[0].iov_len = buff_sz;
      remote[0].iov_len = buff_sz;
      remote[0].iov_base = vm;
      process_vm_readv((pid_t)pid, local, 1, remote, 1, 0);      
      return *buf;
}

std::string read_str_from_mem_block(pid_t pid, void* mb_start, void* mb_end=nullptr){
      char tmp;
      std::string ret = "";
      for(void* i = mb_start; i != mb_end; i = (void*)(((char*)i)+1)){
            tmp = read_char_from_pid_mem(pid, mb_start);
            while(tmp > 0 && tmp < 127){
                  tmp = read_char_from_pid_mem(pid, i);
                  ret += tmp;
                  i = (void*)(((char*)i)+1);
            }
            return ret;
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

mem_map vars_in_mem(pid_t pid, int d_rgn=STACK, bool use_additional_rgns=true, bool integers=true){
      mem_map ret;
      ret.pid = pid;
      ret.mapped_rgn = get_vmem_locations(pid);
      long m_size = 0;
      void* vm_l_stack; void* vm_l_end_stack; void* vm_l_heap; void* vm_l_end_heap;
      if(d_rgn == STACK || d_rgn == BOTH){
            vm_l_stack = ret.mapped_rgn.stack_start_addr;
            vm_l_end_stack = ret.mapped_rgn.stack_end_addr;
            m_size = (char*)vm_l_end_stack-(char*)vm_l_stack;
      }
      if(d_rgn == HEAP || d_rgn == BOTH){
            vm_l_heap = ret.mapped_rgn.heap_start_addr;
            vm_l_end_heap = ret.mapped_rgn.heap_end_addr;
            m_size += ((char*)vm_l_end_heap-(char*)vm_l_heap);
      }
      if(use_additional_rgns){
            for(int i = 0; i < ret.mapped_rgn.n_remaining; ++i){
                  m_size += (char*)ret.mapped_rgn.remaining_addr[i].second-(char*)ret.mapped_rgn.remaining_addr[i].first;
            }
      }
      ret.size = 0;
      if(integers)ret.mmap = new std::pair<void*, int>[m_size];
      else ret.cp_mmap = new std::pair<void*, std::string>[m_size];
      //TODO: find smarter way to allocate string mmap memory, this assumes that each memory location stores an individual string
      long c = 0;
      if(integers){
            ret.size = m_size;
            //int tmp;
            if(d_rgn == STACK || d_rgn == BOTH){
                  int* ints_in_stack = read_ints_from_pid_mem(pid, vm_l_stack, vm_l_end_stack);
                  for(int d = 0; vm_l_stack != vm_l_end_stack; vm_l_stack = (void*)(((char*)vm_l_stack)+1)){
                        std::pair<void*, int> tmp_pair(vm_l_stack, ints_in_stack[d]);
                        ret.mmap[c++] = tmp_pair;
                  }
                  delete[] ints_in_stack;
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  int* ints_in_heap = read_ints_from_pid_mem(pid, vm_l_heap, vm_l_end_heap);
                  for(int d = 0; vm_l_heap != vm_l_end_heap; vm_l_heap = (void*)(((char*)vm_l_heap)+1)){
                        std::pair<void*, int> tmp_pair(vm_l_heap, ints_in_heap[d]);
                        ret.mmap[c++] = tmp_pair;
                  }
                  delete[] ints_in_heap;

            }
            if(use_additional_rgns){
                  for(int i = 0; i < ret.mapped_rgn.n_remaining; ++i){
                        int* ints_in_adtnl = read_ints_from_pid_mem(pid, ret.mapped_rgn.remaining_addr[i].first, 
                                                                         ret.mapped_rgn.remaining_addr[i].second);
                        int d = 0;
                        for(void* vm_l = ret.mapped_rgn.remaining_addr[i].first;
                                  vm_l != ret.mapped_rgn.remaining_addr[i].second;
                                  vm_l = (void*)(((char*)vm_l)+1)){
                              std::pair<void*, int> tmp_pair(vm_l, ints_in_adtnl[d++]);
                              ret.mmap[c++] = tmp_pair;
                        }
                        delete[] ints_in_adtnl;
                  }
            }
      }
      else{
            // when searching for end of string, all permutations of string show up
            std::string tmp;
            if(d_rgn == STACK || d_rgn == BOTH){
                  for(; vm_l_stack != vm_l_end_stack; vm_l_stack = (void*)(((char*)vm_l_stack)+1)){
                        tmp = read_str_from_mem_block(pid, vm_l_stack);
                        std::pair<void*, std::string> tmp_pair(vm_l_stack, tmp);
                        ret.cp_mmap[c++] = tmp_pair;
                        vm_l_stack = (void*)(((char*)vm_l_stack)+tmp.size());
                        ++ret.size;
                  }
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  for(; vm_l_heap != vm_l_end_heap; vm_l_heap = (void*)(((char*)vm_l_heap)+1)){
                        tmp = read_str_from_mem_block(pid, vm_l_heap);
                        std::pair<void*, std::string> tmp_pair(vm_l_heap, tmp);
                        ret.cp_mmap[c++] = tmp_pair;
                        vm_l_heap = (void*)(((char*)vm_l_heap)+tmp.size());
                        ++ret.size;
                  }
            }
            if(use_additional_rgns){
                  for(int i = 0; i < ret.mapped_rgn.n_remaining; ++i){
                        for(void* vm_l = ret.mapped_rgn.remaining_addr[i].first;
                                  vm_l != ret.mapped_rgn.remaining_addr[i].second;
                                  vm_l = (void*)(((char*)vm_l)+1)){
                              tmp = read_str_from_mem_block(pid, vm_l);
                              std::pair<void*, std::string> tmp_pair(vm_l, tmp);
                              ret.cp_mmap[c++] = tmp_pair;
                              vm_l = (void*)(((char*)vm_l)+tmp.size());
                              ++ret.size;
                        }
                  }
            }
      }
      return ret;
}

void update_mem_map(mem_map &mem, bool integers=true){
      if(integers){
            for(int i = 0; i < mem.size; ++i){
                  mem.mmap[i].second = read_int_from_pid_mem(mem.pid, mem.mmap[i].first);
            }
      }
      else{
            for(int i = 0; i < mem.size; ++i){
                  mem.cp_mmap[i].second = read_str_from_mem_block(mem.pid, mem.cp_mmap[i].first);
            }
      }
}

void narrow_mem_map_int(mem_map &mem, int match){
      std::string match_str = std::to_string(match);
      for(int i = 0; i < mem.size; ++i){
            if(std::to_string(mem.mmap[i].second) != match_str){ // exact
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
}

void narrow_mem_map_str(mem_map &mem, std::string match, bool exact=true){
      for(int i = 0; i < mem.size; ++i){
            if(exact){
                  if(mem.cp_mmap[i].second != match){
                        mem.cp_mmap[i--] = mem.cp_mmap[--mem.size];
                  }
            }
            else{
                  if(mem.cp_mmap[i].second.find(match) == std::string::npos){
                        --mem.size;
                        if(mem.size == 0)break;
                        mem.cp_mmap[i--] = mem.cp_mmap[mem.size];
                  }
            }
      }
}
