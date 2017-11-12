#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "vmem_access.h"

#define STACK 0
#define HEAP  1
#define BOTH  2

bool mem_rgn_warn(int d_rgn, mem_rgn mem){
      if((d_rgn == STACK || d_rgn == BOTH) && (mem.stack_start_addr == nullptr || mem.stack_end_addr == nullptr)){
            std::cout << "WARNING: no valid stack memory region was found" << std::endl;
            if(d_rgn == STACK)return false;
      }
      if((d_rgn == HEAP || d_rgn == BOTH) && (mem.heap_start_addr == nullptr || mem.heap_end_addr == nullptr)){
            std::cout << "WARNING: no valid heap memory region was found" << std::endl;
            if(d_rgn == HEAP)return false;
      }
      return true;
}

void print_mmap(const mem_map &mem, std::string contains="", bool integers=true){
      if(integers){
            if(contains != ""){
                  for(int i = 0; i < mem.size; ++i){
                        if(std::to_string(mem.mmap[i].second).find(contains) != std::string::npos){
                              std::cout << mem.mmap[i].first << ": " << mem.mmap[i].second << std::endl;
                        }
                  }
            }
            else{
                  for(int i = 0; i < mem.size; ++i){
                        std::cout << mem.mmap[i].first << ": " << mem.mmap[i].second << std::endl;
                  }
            }
      }
      else{
            if(contains != ""){
                  // TODO: waiiiit, see if .find("") always returns foudn. if so don't need to handle both cases
                  for(int i = 0; i < mem.size; ++i){
                        if(mem.cp_mmap[i].second.find(contains) != std::string::npos){
                              std::cout << mem.cp_mmap[i].first << ": " << mem.cp_mmap[i].second << std::endl;
                        }
                  }
            }
            else{
                  for(int i = 0; i < mem.size; ++i){
                        std::cout << mem.cp_mmap[i].first << ": " << mem.cp_mmap[i].second << std::endl;
                  }
            }
      }
}

// param can be const ref because i'm just using the mem_map as a guide for which mem locations to invert
void logic_swap(const mem_map &mem){
      for(int i = 0; i < mem.size; ++i){
            if(mem.mmap[i].second == 0 || mem.mmap[i].second == 1)
            write_int_to_pid_mem(mem.pid, mem.mmap[i].first, (int)!mem.mmap[i].second);
      }
}

void interactive_mode(mem_map &vmem, bool integers, int d_rgn=STACK){
      std::cout << "in interactive mode on ";
      if(d_rgn == STACK)std::cout << "stack - ";
      if(d_rgn == HEAP)std::cout << "heap - ";
      if(d_rgn == BOTH)std::cout << "both stack and heap - ";
      std::cout << "looking for ";
      if(integers)std::cout << "integers" << std::endl;
      else std::cout << "strings" << std::endl;
      std::string tmp_str;
      int tmp_val;
      while(1){
            Find:
            std::cout << "enter current variable value or 'w' to enter write mode" << std::endl;
            std::getline(std::cin, tmp_str);
            if(tmp_str == "w"){
                  int vl_c;
                  std::string tmp_num, v_loc_s, to_w;
                  int v_loc[2]; // right now v_loc is meant to store start and end of a range
                  while(1){
                        std::cout << "vmem size: " << vmem.size << std::endl;
                        if(integers){
                              for(int i = 0; i < vmem.size; ++i){
                                    std::cout << i << ": (" << vmem.mmap[i].first << ": " << vmem.mmap[i].second << ")" << std::endl;
                              }
                        }
                        else{
                              for(int i = 0; i < vmem.size; ++i){
                                    std::cout << i << ": (" << vmem.cp_mmap[i].first << ": \"" << vmem.cp_mmap[i].second << "\")" << std::endl;
                              }
                        }
                        // TODO: maybe allow multiple values separated by some delim like ','
                        std::cout << "enter a number from [0-" << vmem.size-1 << "] or a range with a '-', followed by value to write OR 's' to continue searching" << std::endl;
                        std::cin >> v_loc_s;
                        if(v_loc_s == "s"){
                              std::cin.clear();
                              std::cin.ignore(1000, '\n');
                              print_mmap(vmem, "", integers);
                              goto Find;
                        }
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
                              if(integers)write_int_to_pid_mem(vmem.pid, vmem.mmap[i].first, std::stoi(to_w));
                              else{
                                    if(to_w.size() > vmem.cp_mmap[i].second.size()){
                                          // not correcting string size for now
                                          std::cout << "WARNING (" << vmem.pid << ":" << vmem.cp_mmap[i].first << "): writing a string that is larger than the original string in its memory location causes undefined behavior" << std::endl; 
                                    }
                                    write_str_to_pid_mem(vmem.pid, vmem.cp_mmap[i].first, to_w);
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
            if(vmem.size == 0){
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
            // stop here if none of our required data regions are available
            if(!mem_rgn_warn(d_rgn, vmem.mapped_rgn))return -1;
            if(strcmp(argv[2], "-p") == 0){
                  if(argc > 3 && argv[3][0] != '-'){
                        print_mmap(vmem, argv[3], integers);
                  }
                  else print_mmap(vmem, "", integers);
                  delete[] &vmem;
                  return 0;
            }
            if(strcmp(argv[2], "-i") == 0){
                  if(!integers){
                        std::cout << "cannot invert string/char*" << std::endl;
                        delete[] &vmem;
                        return -1;
                  }
                  logic_swap(vmem);
                  delete[] &vmem;
                  return 0;
            }
            if(strcmp(argv[2], "-f") == 0){
                  interactive_mode(vmem, integers, d_rgn);
                  delete[] &vmem;
                  return 0;
            }
      }
      mem_map vmem = vars_in_mem((pid_t)std::stoi(argv[1]), d_rgn, integers);
      // stop here if none of our required data regions are available
      if(!mem_rgn_warn(d_rgn, vmem.mapped_rgn))return -1;
      interactive_mode(vmem, integers, d_rgn);
      delete[] &vmem;
      return 0;
}
