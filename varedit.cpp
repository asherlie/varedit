#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <stdio.h>
#include <sys/uio.h>

#include "vmem_parser.h"

#define STACK 0
#define HEAP  1
#define BOTH  2

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

char read_char_from_pid_mem(int pid, void* vm){
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

bool write_str_to_pid_mem(pid_t pid, void* vm, std::string str){
      int s_c = 0;
      unsigned int written = 0; 
      for(void* i = vm; i != (void*)(((char*)vm)+str.size()); i = (void*)((char*)i+1)){
            written += write_int_to_pid_mem(pid, i, str[s_c++]);
      }
      return written == str.size();
}

mem_map vars_in_mem(pid_t pid, int d_rgn=STACK, bool integers=true){
      mem_map ret;
      ret.pid = pid;
      mem_rgn rgn = get_vmem_locations(pid);
      void* vm_l_stack; void* vm_l_end_stack; void* vm_l_heap; void* vm_l_end_heap;
      if(d_rgn == STACK || d_rgn == BOTH){
            vm_l_stack = rgn.stack_start_addr;
            vm_l_end_stack = rgn.stack_end_addr;
      }
      if(d_rgn == HEAP || d_rgn == BOTH){
            vm_l_heap = rgn.heap_start_addr;
            vm_l_end_heap = rgn.heap_end_addr;
      }
      if(integers){
            int tmp;
            if(d_rgn == STACK || d_rgn == BOTH){
                  //           casting to char* to increment, then back to void*
                  for(; vm_l_stack != vm_l_end_stack; vm_l_stack = (void*)(((char*)vm_l_stack)+1)){
                        tmp = read_int_from_pid_mem(pid, vm_l_stack);
                        ret.mmap[vm_l_stack] = tmp;
                  }
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  for(; vm_l_heap != vm_l_end_heap; vm_l_heap = (void*)(((char*)vm_l_heap)+1)){
                        tmp = read_int_from_pid_mem(pid, vm_l_heap);
                        ret.mmap[vm_l_stack] = tmp;
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

void print_mmap(mem_map mem, std::string contains="", bool integers=true){
      if(integers){
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
      else{
            if(contains != ""){
                  for(std::map<void*, std::string>::iterator it = mem.cp_mmap.begin(); it != mem.cp_mmap.end(); ++it){
                        if(it->second.find(contains) != std::string::npos){
                              std::cout << it->first << ": " << it->second << std::endl;
                        }
                  }
            }
            else{
                  for(std::map<void*, std::string>::iterator it = mem.cp_mmap.begin(); it != mem.cp_mmap.end(); ++it){
                        std::cout << it->first << ": " << it->second << std::endl;
                  }
            }
      }
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

void logic_swap(mem_map mem){
      for(std::map<void*, int>::iterator it = mem.mmap.begin(); it != mem.mmap.end(); ++it){
            if(it->second == 0 || it->second == 1)
            write_int_to_pid_mem(mem.pid, it->first, (int)!it->second);
      }
}

void interactive_mode(mem_map &vmem, bool integers, int d_rgn=STACK){
      std::string tmp_str;
      int tmp_val;
      while(1){
            std::cout << "enter current variable value or 'w' to enter write mode" << std::endl;
            std::cin >> tmp_str;
            if(tmp_str == "w"){
                  int c, vl_c;
                  std::string tmp_num, v_loc_s, to_w;
                  int v_loc[2]; // right now v_loc is meant to store start and end of a range
                  while(1){
                        c = 0;
                        // renamed and moved initialization here so n_vm's are in scope when needed TODO: fix this later
                        std::pair<void*, int> n_vm_i[vmem.mmap.size()];
                        std::pair<void*, std::string> n_vm_s[vmem.cp_mmap.size()];
                        if(integers){
                              for(std::map<void*, int>::iterator it = vmem.mmap.begin(); it != vmem.mmap.end(); ++it){
                                    n_vm_i[c] = *it;
                                    std::cout << c++ << ": (" << it->first << ": " << it->second << ")" << std::endl; 
                              }
                        }
                        else{
                              for(std::map<void*, std::string>::iterator it = vmem.cp_mmap.begin(); it != vmem.cp_mmap.end(); ++it){
                                    n_vm_s[c] = *it;
                                    std::cout << c++ << ": (" << it->first << ": \"" << it->second << "\")" << std::endl; 
                              }
                        }
                        // TODO: maybe allow multiple values separated by some delim like ','
                        if(integers)c = vmem.mmap.size()-1;
                        else c = vmem.cp_mmap.size()-1;
                        std::cout << "enter a number from [0-" << c << "] or a range with a '-', followed by value to write" << std::endl;
                        std::cin >> v_loc_s >> to_w;
                        vl_c = 0;
                        tmp_num = "";
                        for(unsigned int i = 0; i < v_loc_s.size(); ++i){
                              if(v_loc_s[i] == '-'){
                                    v_loc[vl_c++] = std::stoi(tmp_num);
                                    tmp_num = "";
                              }
                              else{
                                    tmp_num += v_loc_s[i];
                              }
                        }
                        v_loc[vl_c] = std::stoi(tmp_num);
                        for(int i = v_loc[0]; i <= v_loc[vl_c]; ++i){ // write all ints in range or between commas
                              if(integers)write_int_to_pid_mem(vmem.pid, n_vm_i[i].first, std::stoi(to_w));
                              else write_str_to_pid_mem(vmem.pid, n_vm_s[i].first, to_w);
                        }
                        update_mem_map(vmem, integers); // to make sure accurate values are printed
                  }
            }
            // tmp_str != "w"
            if(integers){
                  tmp_val = std::stoi(tmp_str);
                  narrow_mem_map_int(vmem, tmp_val);
            }
            else{
                  narrow_mem_map_str(vmem, tmp_str, false);
            }
            if(vmem.mmap.empty() && vmem.cp_mmap.empty()){
                  std::cout << "nothing matches your search of: " << tmp_str << std::endl << "resetting mem map" << std::endl;
                  vmem = vars_in_mem(vmem.pid, d_rgn, integers);
            }
            else{
                  std::cout << "matches are now:" << std::endl;
                  print_mmap(vmem, "", integers);
            }
      }
}


int main(int argc, char* argv[]){
      std::string help_str = "NOTE: this program will not work without root privileges\n<pid> {[-p [filter]] [-r <virtual memory address>] [-i] [-w <virtual memory addres> <value>] [-f] [-H] [-B] [-c]}\n    -p : prints all integers in stack with virtual memory addresses. optional filter\n    -r : read single integer from virtual memory address\n    -i : inverts all 1s and 0s in stack\n    -w : writes value to virtual memory address\n    -f : interactively tracks down memory locations of variables\n    -H : use heap instead of stack\n    -B : use both heap and stack\n    -c : use char/string mode\n";
      if(argc == 1 || (argc > 1 && strcmp(argv[1], "-h") == 0)){
            std::cout << help_str;
            return -1;
      }
      bool integers = true;
      int d_rgn = STACK;
      for(int i = 0; i < argc; ++i){
            if(strcmp(argv[i], "-c") == 0){
                  integers = false;
            }
            if(strcmp(argv[i], "-H") == 0){
                  d_rgn = HEAP;
            }
            if(strcmp(argv[i], "-B") == 0){
                  d_rgn = BOTH;
            }
      }
      mem_map vmem = vars_in_mem((pid_t)std::stoi(argv[1]), d_rgn, integers);
      
      if(argc > 2){
            if(strcmp(argv[2], "-p") == 0){
                  if(integers){
                        if(argc == 4){
                              print_mmap(vmem, argv[3], integers);
                              return 0;
                        }
                  }
                  if(argc == 5){
                       print_mmap(vmem, argv[3], integers);
                       return 0;
                  }
                  print_mmap(vmem, "", integers);
                  return 0;
            }
            else if(strcmp(argv[2], "-r") == 0){
                  if(integers){
                        std::cout << vmem.mmap[(void*)strtoul(argv[3], 0, 16)] << std::endl;
                  }
                  else{
                        std::cout << vmem.cp_mmap[(void*)strtoul(argv[3], 0, 16)] << std::endl;
                  }
                  return 0;
            }
            else if(strcmp(argv[2], "-i") == 0){
                  if(!integers){
                        std::cout << "cannot invert string/char*" << std::endl;
                        return -1;
                  }
                  logic_swap(vmem);
                  return 0;
            }
            else if(strcmp(argv[2], "-w") == 0){
                  if(integers){
                        write_int_to_pid_mem(vmem.pid, (void*)strtoul(argv[3], 0, 16), std::stoi(argv[4]));
                        return 0;
                  }
                  write_str_to_pid_mem(vmem.pid, (void*)strtoul(argv[3], 0, 16), argv[4]);
                  return 0;
            }
            else if(strcmp(argv[2], "-f") == 0){
                  interactive_mode(vmem, integers, d_rgn);
                  return 0;
            }
      }
      interactive_mode(vmem, integers, d_rgn);
      return 0;
}
