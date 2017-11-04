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
      remote[0].iov_base = vm;
      process_vm_readv((pid_t)pid, local, 1, remote, 1, 0);
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
      process_vm_readv((pid_t)pid, local, 1, remote, 1, 0);
      return *buf;
}

bool write_int_to_pid_mem(int pid, void* vm, int value){
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

mem_map ints_in_mem(pid_t pid, bool stack=true){
      mem_map ret;
      ret.pid = pid;
      mem_rgn rgn = get_vmem_locations(pid);
      void* vm_l;
      void* vm_l_end;
      if(stack){
            vm_l = rgn.stack_start_addr;
            vm_l_end = rgn.stack_end_addr;
      }
      else{
            vm_l = rgn.heap_start_addr;
            vm_l_end = rgn.heap_end_addr;
      }
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

void update_mem_map(mem_map &mem){
      for(std::map<void*, int>::iterator it = mem.mmap.begin(); it != mem.mmap.end(); ++it){
            it->second = read_int_from_pid_mem(mem.pid, it->first);
      }
}

void narrow_mem_map(mem_map &mem, int match){
      std::string match_str = std::to_string(match);
      for(std::map<void*, int>::iterator it = mem.mmap.begin(); it != mem.mmap.end(); ++it){
            //if(std::to_string(it->second).find(match_str) == std::string::npos){ // contains
            if(std::to_string(it->second) != match_str){ // exact
                  mem.mmap.erase(it);
            }
      }
}

void logic_swap(mem_map mem){
      for(std::map<void*, int>::iterator it = mem.mmap.begin(); it != mem.mmap.end(); ++it){
            if(it->second == 0 || it->second == 1)
            write_int_to_pid_mem(mem.pid, it->first, (int)!it->second);
      }
}



int main(int argc, char* argv[]){
      std::string help_str = "NOTE: this program will not work without root privileges\n<pid> {[-p [filter]] [-r <virtual memory address>] [-i] [-w <virtual memory addres> <value>] [-f] [-H]}\n    -p : prints all integers in stack with virtual memory addresses. optional filter\n    -r : read single integer from virtual memory address\n    -i : inverts all 1s and 0s in stack\n    -w : writes value to virtual memory address\n    -f : interactively tracks down memory locations of variables\n    -H : use heap instead of stack\n";
      if(argc == 1 || (argc > 1 && strcmp(argv[1], "-h") == 0)){
            std::cout << help_str;
            return -1;
      }
      bool stack = true;
      if(strcmp(argv[argc-1], "-H") == 0)stack = false;
      mem_map vmem = ints_in_mem((pid_t)std::stoi(argv[1]), stack);
      
      if(argc > 2){
            if(strcmp(argv[2], "-p") == 0){
                  if(argc == 4){
                        print_mmap(vmem, argv[3]);
                        return 0;
                  }
                  print_mmap(vmem);
                  return 0;
            }
            else if(strcmp(argv[2], "-r") == 0){
                  std::cout << vmem.mmap[(void*)strtoul(argv[3], 0, 16)] << std::endl;
                  return 0;
            }
            else if(strcmp(argv[2], "-i") == 0){
                  logic_swap(vmem);
                  return 0;
            }
            else if(strcmp(argv[2], "-w") == 0){
                  write_int_to_pid_mem(vmem.pid, (void*)strtoul(argv[3], 0, 16), std::stoi(argv[4]));
                  return 0;
            }
            else if(strcmp(argv[2], "-f") == 0){
            f:
                  std::string tmp_str;
                  int tmp_val;
                  while(1){
                        std::cout << "enter current variable value or 'w' to enter write mode" << std::endl;
                        std::cin >> tmp_str;
                        if(tmp_str == "w"){
                              int c, to_w, vl_c;
                              std::string tmp_num, v_loc_s;
                              int v_loc[2]; // right now v_loc is meant to store start and end of a range
                              while(1){
                                    c = 0;
                                    std::pair<void*, int> n_vm[vmem.mmap.size()];
                                    for(std::map<void*, int>::iterator it = vmem.mmap.begin(); it != vmem.mmap.end(); ++it){
                                          n_vm[c] = *it;
                                          std::cout << c++ << ": (" << it->first << ": " << it->second << ")" << std::endl; 
                                    }
                                    // TODO: maybe allow multiple values separated by some delim like ','
                                    std::cout << "enter a number from [0-" << vmem.mmap.size()-1 << "] or a range with a '-', followed by value to write" << std::endl;
                                    std::cin >> v_loc_s >> to_w;
                                    vl_c = 0;
                                    tmp_num = "";
                                    for(unsigned int i = 0; i < v_loc_s.size(); ++i){
                                          if(v_loc_s[i] == '-'){
                                                //range_mode = true;
                                                v_loc[vl_c++] = std::stoi(tmp_num);
                                                tmp_num = "";
                                          }
                                          else{
                                                tmp_num += v_loc_s[i];
                                          }
                                    }
                                    v_loc[vl_c] = std::stoi(tmp_num);
                                    for(int i = v_loc[0]; i <= v_loc[vl_c]; ++i){ // write all ints in range or between commas
                                          write_int_to_pid_mem(vmem.pid, n_vm[i].first, to_w);
                                    }
                                    update_mem_map(vmem); // to make sure accurate values are printed
                              }
                        }
                        tmp_val = std::stoi(tmp_str);
                        narrow_mem_map(vmem, tmp_val);
                        if(vmem.mmap.empty()){
                              std::cout << "nothing matches your search of: " << tmp_val << std::endl << "resetting mem map" << std::endl;
                              vmem = ints_in_mem(vmem.pid, stack);

                        }
                        else{
                              std::cout << "matches are now:" << std::endl;
                              print_mmap(vmem);
                        }
                  }
                  return 0;
            }
      }
      goto f; //default to find var/interactive mode
}
