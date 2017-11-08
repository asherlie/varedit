#include <iostream>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "vmem_access.h"

#define STACK 0
#define HEAP  1
#define BOTH  2

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

void logic_swap(mem_map mem){
      for(std::map<void*, int>::iterator it = mem.mmap.begin(); it != mem.mmap.end(); ++it){
            if(it->second == 0 || it->second == 1)
            write_int_to_pid_mem(mem.pid, it->first, (int)!it->second);
      }
}

void interactive_mode(mem_map &vmem, bool integers, int d_rgn=STACK){
      std::cout << "in interactive mode on ";
      if(d_rgn == STACK)std::cout << "stack - ";
      if(d_rgn == HEAP)std::cout << "heap - ";
      if(d_rgn == BOTH)std::cout << "both stack and heap - ";
      std::cout << "using ";
      if(integers)std::cout << "integers" << std::endl;
      else std::cout << "strings" << std::endl;
      std::string tmp_str;
      int tmp_val;
      while(1){
            std::cout << "enter current variable value or 'w' to enter write mode" << std::endl;
            std::getline(std::cin, tmp_str);
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
                        std::cin >> v_loc_s;
                        // std::ws to get rid of leading whitespace
                        std::getline(std::cin >> std::ws, to_w);
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
                              else{
                                    if(to_w.size() > n_vm_s[i].second.size()){
                                          // not correcting string size for now
                                          std::cout << "WARNING: writing a string that is larger than the original string in its memory location will cause undefined behavior" << std::endl; 
                                    }
                                    write_str_to_pid_mem(vmem.pid, n_vm_s[i].first, to_w);
                              }
                        }
                        update_mem_map(vmem, integers); // to make sure accurate values are printed
                  }
            }
            // tmp_str != "w"
            update_mem_map(vmem, integers);
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
      if(argc > 2){
            // -r and -w can be done without slowly loading a complete mem_map
            if(strcmp(argv[2], "-r") == 0){
                  if(integers)std::cout << read_int_from_pid_mem((pid_t)std::stoi(argv[1]), (void*)strtoul(argv[3], 0, 16)) << std::endl;
                  else std::cout << read_str_from_mem_block((pid_t)std::stoi(argv[1]), (void*)strtoul(argv[3], 0, 16), nullptr) << std::endl;
                  return 0;
            }
            if(strcmp(argv[2], "-w") == 0){
                  if(integers)write_int_to_pid_mem((pid_t)std::stoi(argv[1]), (void*)strtoul(argv[3], 0, 16), std::stoi(argv[4]));
                  else write_str_to_pid_mem((pid_t)std::stoi(argv[1]), (void*)strtoul(argv[3], 0, 16), argv[4]);
                  return 0;
            }
            // mem_map is needed for all other flags
            mem_map vmem = vars_in_mem((pid_t)std::stoi(argv[1]), d_rgn, integers);
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
            if(strcmp(argv[2], "-i") == 0){
                  if(!integers){
                        std::cout << "cannot invert string/char*" << std::endl;
                        return -1;
                  }
                  logic_swap(vmem);
                  return 0;
            }
            if(strcmp(argv[2], "-f") == 0){
                  interactive_mode(vmem, integers, d_rgn);
                  return 0;
            }
      }
      mem_map vmem = vars_in_mem((pid_t)std::stoi(argv[1]), d_rgn, integers);
      interactive_mode(vmem, integers, d_rgn);
      return 0;
}
