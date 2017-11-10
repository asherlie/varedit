#include "vmem_access.h"
#include <math.h>
#include <sys/uio.h>
#include <iostream>

#define STACK 0
#define HEAP 1
#define BOTH 2

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
mem_map vars_in_mem(pid_t pid, int d_rgn, bool integers, bool verbose){
      mem_map ret;
      ret.pid = pid;
      ret.mapped_rgn = get_vmem_locations(pid);
      void* vm_l_stack; void* vm_l_end_stack; void* vm_l_heap; void* vm_l_end_heap;
      if(d_rgn == STACK || d_rgn == BOTH){
            vm_l_stack = ret.mapped_rgn.stack_start_addr;
            vm_l_end_stack = ret.mapped_rgn.stack_end_addr;
      }
      if(d_rgn == HEAP || d_rgn == BOTH){
            vm_l_heap = ret.mapped_rgn.heap_start_addr;
            vm_l_end_heap = ret.mapped_rgn.heap_end_addr;
      }
      if(integers){
            int tmp;
            if(d_rgn == STACK || d_rgn == BOTH){
                  float s_dlen = (char*)vm_l_end_stack-(char*)vm_l_stack;
                  float c=0;
                  //           casting to char* to increment, then back to void*
                  for(; vm_l_stack != vm_l_end_stack; vm_l_stack = (void*)(((char*)vm_l_stack)+1)){
                        tmp = read_int_from_pid_mem(pid, vm_l_stack);
                        ret.mmap[vm_l_stack] = tmp;
                        if(verbose){
                              std::cout << std::round((++c/s_dlen)*100) << "\% of stack scanned\r";
                        }
                  }
            }
            std::cout << "                       \r";
            if(d_rgn == HEAP || d_rgn == BOTH){
                  float s_dlen = (char*)vm_l_end_heap-(char*)vm_l_heap;
                  float c=0;
                  for(; vm_l_heap != vm_l_end_heap; vm_l_heap = (void*)(((char*)vm_l_heap)+1)){
                        tmp = read_int_from_pid_mem(pid, vm_l_heap);
                        ret.mmap[vm_l_stack] = tmp;
                        if(verbose){
                              std::cout << std::round((++c/s_dlen)*100) << "\% of heap scanned\r";
                        }
                  }
            }
      }
      else{
            std::string tmp;
            if(d_rgn == STACK || d_rgn == BOTH){
                  for(; vm_l_stack != vm_l_end_stack; vm_l_stack = (void*)(((char*)vm_l_stack)+1)){
                        tmp = read_str_from_mem_block(pid, vm_l_stack);
                        ret.cp_mmap[vm_l_stack] = tmp; // i can do this bc vm_l stores the first mem addr of the string
                        vm_l_stack = (void*)(((char*)vm_l_stack)+tmp.size());
                  }
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  for(; vm_l_heap != vm_l_end_heap; vm_l_heap = (void*)(((char*)vm_l_heap)+1)){
                        tmp = read_str_from_mem_block(pid, vm_l_heap);
                        ret.cp_mmap[vm_l_heap] = tmp; // i can do this bc vm_l stores the first mem addr of the string
                        vm_l_heap = (void*)(((char*)vm_l_heap)+tmp.size());
                  }
            }
      }
      return ret;
}

void update_mem_map(mem_map &mem, bool integers=true){
      if(integers){
            for(std::map<void*, int>::iterator it = mem.mmap.begin(); it != mem.mmap.end(); ++it){
                  it->second = read_int_from_pid_mem(mem.pid, it->first);
            }
      }
      else{
            for(std::map<void*, std::string>::iterator it = mem.cp_mmap.begin(); it != mem.cp_mmap.end(); ++it){
                  it->second = read_str_from_mem_block(mem.pid, it->first);
            }
      }
}

void narrow_mem_map_int(mem_map &mem, int match){
      std::string match_str = std::to_string(match);
      for(std::map<void*, int>::iterator it = mem.mmap.begin(); it != mem.mmap.end(); ++it){
            //if(std::to_string(it->second).find(match_str) == std::string::npos){ // contains
            if(std::to_string(it->second) != match_str){ // exact
                  mem.mmap.erase(it);
            }
      }
}

void narrow_mem_map_str(mem_map &mem, std::string match, bool exact=true){
      for(std::map<void*, std::string>::iterator it = mem.cp_mmap.begin(); it != mem.cp_mmap.end(); ++it){
            if(exact){
                  if(it->second != match)mem.cp_mmap.erase(it);
            }
            else{
                  if(it->second.find(match) == std::string::npos)mem.cp_mmap.erase(it);
            }
      }
}
