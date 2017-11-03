#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <stdio.h>
#include <sys/uio.h>

#include "vmem_parser.h"

int read_int_from_pid_mem(int pid, void* vm){
      int buff_sz = 4; // sizeof int
      int buf[buff_sz];
      struct iovec local[1];
      struct iovec remote[1];
      local[0].iov_base = buf;
      local[0].iov_len = buff_sz;
      remote[0].iov_len = buff_sz;
      remote[0].iov_base = vm; // if using void* for vm param
      ssize_t nread = process_vm_readv((pid_t)pid, local, 1, remote, 1, 0);
      return *buf;
}

char read_str_from_pid_mem(int pid, void* vm, int strlen){
      int buff_sz = strlen;
      char buf[buff_sz];
      struct iovec local[1];
      struct iovec remote[1];
      local[0].iov_base = buf;
      local[0].iov_len = buff_sz;
      remote[0].iov_len = buff_sz;
      remote[0].iov_base = (void*)vm;
      ssize_t nread = process_vm_readv((pid_t)pid, local, 1, remote, 1, 0);
      return *buf;
}

int write_int_from_pid_mem(int pid, void* vm, int value){
      int buff_sz = 4; // sizeof int
      int buf[buff_sz];
      buf[0] = value;
      struct iovec local[1];
      struct iovec remote[1];
      local[0].iov_base = buf;
      local[0].iov_len = buff_sz;
      remote[0].iov_len = buff_sz;
      remote[0].iov_base = vm;
      ssize_t nread = process_vm_writev((pid_t)pid, local, 1, remote, 1, 0);
}

mem_map ints_in_stack(pid_t pid){
      mem_map ret;
      ret.pid = pid;
      mem_rgn rgn = get_vmem_locations(pid);
      void* vm_l = rgn.stack_start_addr;
      void* vm_l_end = rgn.stack_end_addr;
      int tmp;
      //           casting to char* to increment, then back to void*
      for(; vm_l != vm_l_end; vm_l = (void*)(((char*)vm_l)+1)){
            tmp = read_int_from_pid_mem(pid, vm_l);
            ret.mmap[vm_l] = tmp;
      }
      return ret;
}

void print_mmap(mem_map mem, std::string contains=""){
      if(contains != ""){
            for(std::map<void*, int>::iterator it = mem.mmap.begin(); it != mem.mmap.end(); ++it){
                  if(std::to_string(it->second).find(contains) != std::string::npos){
                        std::cout << it->first << ": " << it->second << std::endl; 
                  }
            }
      }
      else{
            for(std::map<void*, int>::iterator it = mem.mmap.begin(); it != mem.mmap.end(); ++it){
                  std::cout << it->first << ": " << it->second << std::endl; 
            }
      }
}

void logic_swap(mem_map mem){
      for(std::map<void*, int>::iterator it = mem.mmap.begin(); it != mem.mmap.end(); ++it){
            if(it->second == 0 || it->second == 1)
            write_int_from_pid_mem(mem.pid, it->first, (int)!it->second);
      }
}

int main(int argc, char* argv[]){
      // TODO: write help string
      std::string help_str = "";
      if(argc == 1){
            std::cout << help_str;
            return -1;
      }
      // TODO: add interactive mode
      // TODO: add -f mode to track down variables
      mem_map vmem = ints_in_stack((pid_t)std::stoi(argv[1]));
      if(argc > 2){
            if(strcmp(argv[2], "-p") == 0){
                  if(argc == 4){
                        print_mmap(vmem, argv[3]);
                        return 0;
                  }
                  print_mmap(vmem);
                  return 0;
            }
            else if(strcmp(argv[2], "-i") == 0){
                  logic_swap(vmem);
                  return 0;
            }
            else if(strcmp(argv[2], "-w") == 0){
                  write_int_from_pid_mem(vmem.pid, (void*)strtoul(argv[3], 0, 16), std::stoi(argv[4]));
                  return 0;
            }
      }
      print_mmap(vmem);
}
