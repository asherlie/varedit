#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h> // fork
#include <fstream> // -sb/-wb

#include "vmem_access.h"

#define STACK 0
#define HEAP  1
#define BOTH  2
#define NONE  3

bool mem_rgn_warn(int d_rgn, mem_rgn mem, bool additional){
      bool no_ad = (mem.n_remaining == 0 && additional);
      bool stack = true;
      if(no_ad)std::cout << "WARNING: no valid unmarked memory regions were found" << std::endl;
      if((d_rgn == STACK || d_rgn == BOTH) && (mem.stack_start_addr == nullptr || mem.stack_end_addr == nullptr)){
            std::cout << "WARNING: no valid stack memory region was found" << std::endl;
            if(d_rgn == STACK && (no_ad || !additional))return false;
            stack = false;
      }
      if((d_rgn == HEAP || d_rgn == BOTH) && (mem.heap_start_addr == nullptr || mem.heap_end_addr == nullptr)){
            std::cout << "WARNING: no valid heap memory region was found" << std::endl;
            if((d_rgn == HEAP || (d_rgn == BOTH && !stack)) && (no_ad || !additional))return false;
      }
      return true;
}

void save_pid_mem_state(const mem_map &vmem, std::string outf){
      std::ofstream ofs(outf);
      for(int i = 0; i < vmem.size; ++i){
            if(vmem.mmap[i].first == 0)break; // TODO: make sure the <0, 0> pairs are always at the end of the mem rgn.
            // if(vmem.mmap[i].first != 0){   //       if not, use this. would be a bit slower
            ofs << vmem.mmap[i].first << " " << vmem.mmap[i].second << "\n";
      }
      ofs.close();
}

void restore_pid_mem_state(pid_t pid, std::string inf, bool verbose){
      std::ifstream ifs(inf);
      std::string tmp_addr;
      int tmp_i;
      while(ifs >> tmp_addr >> tmp_i){
            if(tmp_addr != "0"){
            //if(tmp_addr != "0" && tmp_i != 0 && tmp_i != 1){ // TODO: should i write 1's and 0's?
                  if(verbose){
                        if(tmp_i != read_single_int_from_pid_mem(pid, (void*)strtoul(tmp_addr.c_str(), 0, 16))){
                              std::cout << tmp_addr << ": " << read_single_int_from_pid_mem(pid, (void*)strtoul(tmp_addr.c_str(), 0, 16)) << " -> " << tmp_i << std::endl;
                        }
                  }
                  write_int_to_pid_mem(pid, (void*)strtoul(tmp_addr.c_str(), 0, 16), tmp_i);
            }
      }
      ifs.close();
}

void print_mmap(const mem_map &mem, std::string contains="", bool integers=true){
      for(int i = 0; i < mem.size; ++i){
            if(integers){
                  if(std::to_string(mem.mmap[i].second).find(contains) != std::string::npos){
                        std::cout << mem.mmap[i].first << ": " << mem.mmap[i].second << std::endl;
                  }
            }
            else{
                  if(mem.cp_mmap[i].second.find(contains) != std::string::npos){
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

void interactive_mode(mem_map &vmem, bool integers, int d_rgn=STACK, int additional=true){
      std::cout << "in interactive mode on process " << vmem.pid << " (" << vmem.mapped_rgn.p_name << ")\nusing ";
      if(d_rgn == STACK)std::cout << "stack";
      if(d_rgn == HEAP)std::cout << "heap";
      if(d_rgn == BOTH)std::cout << "both stack and heap";
      if(additional && vmem.mapped_rgn.n_remaining != 0)std::cout << " as well as " << vmem.mapped_rgn.n_remaining << " additional memory regions";
      std::cout << " - looking for ";
      if(integers)std::cout << "integers" << std::endl;
      else std::cout << "strings" << std::endl;
      std::string tmp_str;
      int tmp_val;
      bool first = true;
      bool lock_mode;
      while(1){
            Find:
            std::cout << "enter current variable value or 'w' to enter write mode" << std::endl;
            // TODO:
            // maybe enter u to update current values without searching. add both here and in write mode
            //also, enable escaping u/w with \u and \w for searching for the letters u and w
            std::getline(std::cin, tmp_str);
            if(tmp_str == "w"){
                  int vl_c;
                  std::string tmp_num, v_loc_s, to_w;
                  int v_loc[2]; // right now v_loc is meant to store start and end of a range
                  while(1){
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
                        lock_mode = false;
                        if(v_loc_s == "l"){
                              lock_mode = true;
                              std::cin >> v_loc_s;
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
                        if(lock_mode){
                              if(fork() == 0){ // TODO: kill this if overwriting the same mem location
                                    int to_w_i = std::stoi(to_w);
                                    while(1){ // child process will forever repeat this
                                          for(int i = v_loc[0]; i <= v_loc[vl_c]; ++i){
                                                if(integers)write_int_to_pid_mem(vmem.pid, vmem.mmap[i].first, to_w_i);
                                                else write_str_to_pid_mem(vmem.pid, vmem.cp_mmap[i].first, to_w);
                                          }
                                    }
                              }
                              std::cout << "variable(s) locked" << std::endl;
                              update_mem_map(vmem, integers);
                              // TODO: decide what behavior should be after vars have been locked
                              //goto Find;
                              continue;
                        }
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
            if(!first){
                  update_mem_map(vmem, integers);
            }
            if(integers){
                  tmp_val = std::stoi(tmp_str);
                  narrow_mem_map_int(vmem, tmp_val);
            }
            else{
                  narrow_mem_map_str(vmem, tmp_str, false);
            }
            if(vmem.size == 0){
                  std::cout << "nothing matches your search of: " << tmp_str << std::endl << "resetting mem map" << std::endl;
                  vmem = vars_in_mem(vmem.pid, d_rgn, additional, integers);
            }
            else{
                  std::cout << "matches are now:" << std::endl;
                  print_mmap(vmem, "", integers);
            }
            first = false;
      }
}


int main(int argc, char* argv[]){
      std::string help_str = "NOTE: this program will not work without root privileges\n<pid> {[-p [filter]] [-r <virtual memory address>] [-i] [-w <virtual memory addres> <value>] [-f] [-sb <filename>] [-wb <filename>] [-S] [-H] [-B] [-A] [-E] [-C] [-v]}\n    -p : prints all variables in specified memory region with corresopnding virtual memory addresses. optional filter\n    -r : read single value from virtual memory address\n    -i : inverts all 1s and 0s in specified memory region\n    -w : writes value to virtual memory address\n    -f : interactive mode (default)\n    -sb : save backup of process memory to file\n    -wb : restore process memory to backup\n    -S : use stack (default)\n    -H : use heap\n    -B : use both heap and stack\n    -A : look for additional momory regions\n    -E : use all available memory regions\n    -C : use char/string mode\n";
      if(argc == 1 || (argc > 1 && strcmp(argv[1], "-h") == 0)){
            std::cout << help_str;
            return -1;
      }
      bool integers = true, additional=false, verbose=false;
      // TODO: initialize d_rgn to NONE and handle that case
      int d_rgn = STACK;
      for(int i = 0; i < argc; ++i){
            if(strcmp(argv[i], "-S") == 0){
                  d_rgn = STACK;
            }
            if(strcmp(argv[i], "-H") == 0){
                  d_rgn = HEAP;
            }
            if(strcmp(argv[i], "-B") == 0){
                  d_rgn = BOTH;
            }
            if(strcmp(argv[i], "-A") == 0){
                  additional = true;
            }
            if(strcmp(argv[i], "-E") == 0){
                  additional = true;
                  d_rgn = BOTH;
            }
            if(strcmp(argv[i], "-C") == 0){
                  integers = false;
            }
            if(strcmp(argv[i], "-v") == 0){
                  verbose = true;
            }
      }
      // initializing here extends scope to default behavior to avoid rescanning memory
      mem_map vmem;
      if(argc > 2){
            // -r and -w can be done without slowly loading a complete mem_map
            if(strcmp(argv[2], "-r") == 0){
                  if(integers)std::cout << read_single_int_from_pid_mem((pid_t)std::stoi(argv[1]), (void*)strtoul(argv[3], 0, 16)) << std::endl;
                  else std::cout << read_str_from_mem_block_slow((pid_t)std::stoi(argv[1]), (void*)strtoul(argv[3], 0, 16), nullptr) << std::endl;
                  return 0;
            }
            if(strcmp(argv[2], "-w") == 0){
                  if(integers)write_int_to_pid_mem((pid_t)std::stoi(argv[1]), (void*)strtoul(argv[3], 0, 16), std::stoi(argv[4]));
                  else write_str_to_pid_mem((pid_t)std::stoi(argv[1]), (void*)strtoul(argv[3], 0, 16), argv[4]);
                  return 0;
            }
            if(strcmp(argv[2], "-wb") == 0){
                  restore_pid_mem_state((pid_t)std::stoi(argv[1]), argv[3], verbose);
                  return 0;
            }
            // mem_map is needed for all other flags
            vmem = vars_in_mem((pid_t)std::stoi(argv[1]), d_rgn, additional, integers);
            // stop here if none of our required data regions are available
            if(strcmp(argv[2], "-sb") == 0){
                  save_pid_mem_state(vmem, argv[3]);
                  delete[] vmem.mmap;
                  return 0;
            }
            if(!mem_rgn_warn(d_rgn, vmem.mapped_rgn, additional))return -1;
            if(strcmp(argv[2], "-p") == 0){
                  if(argc > 3 && argv[3][0] != '-'){
                        print_mmap(vmem, argv[3], integers);
                  }
                  else print_mmap(vmem, "", integers);
                  if(integers)delete[] vmem.mmap;
                  else delete[] vmem.cp_mmap;
                  delete[] vmem.mapped_rgn.remaining_addr;
                  return 0;
            }
            if(strcmp(argv[2], "-i") == 0){
                  if(!integers){
                        std::cout << "cannot invert string/char*" << std::endl;
                        if(integers)delete[] vmem.mmap;
                        else delete[] vmem.cp_mmap;
                        delete[] vmem.mapped_rgn.remaining_addr;
                        return -1;
                  }
                  logic_swap(vmem);
                  if(integers)delete[] vmem.mmap;
                  else delete[] vmem.cp_mmap;
                  delete[] vmem.mapped_rgn.remaining_addr;
                  return 0;
            }
            if(strcmp(argv[2], "-f") == 0){
                  interactive_mode(vmem, integers, d_rgn, additional);
                  if(integers)delete[] vmem.mmap;
                  else delete[] vmem.cp_mmap;
                  delete[] vmem.mapped_rgn.remaining_addr;
                  return 0;
            }
      }
      //argc <= 2
      else{
            vmem = vars_in_mem((pid_t)std::stoi(argv[1]), d_rgn, additional, integers);
            if(!mem_rgn_warn(d_rgn, vmem.mapped_rgn, additional))return -1;
      }
      // stop here if none of our required data regions are available
      interactive_mode(vmem, integers, d_rgn, additional);
      if(integers)delete[] vmem.mmap;
      else delete[] vmem.cp_mmap;
      delete[] vmem.mapped_rgn.remaining_addr;
      return 0;
}
