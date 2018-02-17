#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h> // fork
#include <fstream>  // -sb/-wb

#include <signal.h> // kill()
#include <sys/wait.h> // wait()

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
                        if(tmp_i != read_single_val_from_pid_mem(pid, 4, (void*)strtoul(tmp_addr.c_str(), 0, 16))){
                              std::cout << tmp_addr << ": " << read_single_val_from_pid_mem(pid, 4, (void*)strtoul(tmp_addr.c_str(), 0, 16)) << " -> " << tmp_i << std::endl;
                        }
                  }
                  write_int_to_pid_mem(pid, (void*)strtoul(tmp_addr.c_str(), 0, 16), tmp_i);
            }
      }
      ifs.close();
}

int remove_volatile_values(mem_map &vmem){
      int n = 0;
      for(int i = 0; i < vmem.size; ++i){
            for(int in = 0; in < 10; ++in){
                  if(vmem.mmap[i].second != read_single_val_from_pid_mem(vmem.pid, 4, vmem.mmap[i].first)){
                        vmem.mmap[i--] = vmem.mmap[--vmem.size];
                        ++n;
                  }
            }
      }
      return n;
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

bool interactive_mode(mem_map &vmem, bool integers, int d_rgn=STACK, int additional=true, bool verbose=false){
      std::string search_mode_help = "search mode options:\n    \"wa\" <value> : write single value to all current results\n    ";
      if(integers)search_mode_help += "<integer> : enter an integer to narrow results\n    \"rv\" : remove volatile variables\n    ";
      else search_mode_help += "<string> : enter a string to narrow results - use delimeter '\\' to search for '?', 'q', 'u', \"rl\", 'w'\n    ";
      search_mode_help += "'u' : update visible values\n    \"rl\" : remove most recently applied lock\n    '?' : show this";
      std::string write_mode_help = "NOTE: <memory location reference #> can be replaced with <start reference #>-<end reference #>\nwrite mode options:\n    <memory location reference #> <value to write> : writes value to memory location(s)\n    l <memory location reference #> <value to write> : locks memory location(s) to provided value\n    l <memory location reference #> _ : locks memory location(s) to their current value(s)\n    '?' : show this";
      std::cout << "in interactive mode on process " << vmem.pid << " (" << vmem.mapped_rgn.p_name << ")\nusing ";
      if(d_rgn == STACK)std::cout << "stack";
      if(d_rgn == HEAP)std::cout << "heap";
      if(d_rgn == BOTH)std::cout << "both stack and heap";
      if(additional && vmem.mapped_rgn.n_remaining != 0)std::cout << " as well as " << vmem.mapped_rgn.n_remaining << " additional memory regions";
      std::cout << " - looking for ";
      if(integers)std::cout << "integers" << std::endl;
      else std::cout << "strings" << std::endl;
      std::cout << "enter 'u' at any time to update visible values, 'q' to exit or '?' for help" << std::endl;
      std::string tmp_str;
      int tmp_val;
      bool first = true;
      bool lock_mode;
      int result_print_limit = 1000; 
      // the three variables below are used to keep track of var locks
      // TODO: decide if i want to keep this ridiculous child_pid storage system or just switch to an array of pid_t's and not print info at removal
      // or switch to this std::pair<pid_t, std::pair<void*, std::string> >[30] child_pid; //and just cast as mentioned below
      std::pair<std::pair<pid_t, std::pair<void*, int> >[30], std::pair<pid_t, std::pair<void*, std::string> >[30]> child_pid;
      pid_t temp_pid;
      int num_locks = 0;
      while(1){
            Find:
            std::cout << "enter current variable value or 'w' to enter write mode" << std::endl;
            std::cin >> tmp_str;
            if(tmp_str == "q")return !first;
            if(tmp_str == "?"){
                  std::cout << search_mode_help << std::endl;
                  goto Find;
            }
            if(tmp_str == "u"){
                  update_mem_map(vmem, integers);
                  std::cin.clear();
                  print_mmap(vmem, "", integers);
                  goto Find;
            }
            if(tmp_str == "r"){
                  if(vmem.size != 0){
                        if(integers)delete[] vmem.mmap;
                        else delete[] vmem.cp_mmap;
                        vmem.size = 0;
                        first = true;
                  }
                  std::cout << "mem map has been reset" << std::endl;
                  std::cin.clear();
                  goto Find;
            }
            // TODO: decide if i want to allow removal of locks in search mode
            if(tmp_str == "rl"){
                  if(num_locks == 0)std::cout << "no locks are currently in place" << std::endl;
                  else {
                        if(integers){
                              kill(child_pid.first[--num_locks].first, SIGKILL);
                              std::cout << "lock with value " << child_pid.first[num_locks].second.second << " removed (" << child_pid.first[num_locks].second.first << ")" << std::endl;
                        }
                        else{
                              kill(child_pid.second[--num_locks].first, SIGKILL);
                              std::cout << "lock with value \"" << child_pid.second[num_locks].second.second << "\" removed (" << child_pid.second[num_locks].second.first << ")" << std::endl;
                        }
                        wait(NULL);
                  }
                  std::cin.clear();
                  goto Find;
            }
            if(tmp_str == "wa"){
                  std::cin >> tmp_str;
                  if(integers)tmp_val = std::stoi(tmp_str);
                  for(int i = 0; i < vmem.size; ++i){
                        if(integers)write_int_to_pid_mem(vmem.pid, vmem.mmap[i].first, tmp_val);
                        else write_str_to_pid_mem(vmem.pid, vmem.cp_mmap[i].first, tmp_str);
                  }
                  std::cout << "wrote " << tmp_str << " to " << vmem.size << " memory locations" << std::endl;
                  std::cin.ignore(1000, '\n');
                  goto Find;
            }
            if(tmp_str == "w"){
                  int vl_c;
                  std::string tmp_num, v_loc_s, to_w;
                  int v_loc[2]; // v_loc stores start and end of a range
                  while(1){
                        Write:
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
                        std::cout << "enter a number from [0-" << vmem.size-1 << "] or a range with a '-', followed by value to write OR 's' to continue searching" << std::endl;
                        std::cin >> v_loc_s;
                        if(v_loc_s == "s"){
                              std::cin.clear();
                              std::cin.ignore(1000, '\n');
                              print_mmap(vmem, "", integers);
                              goto Find;
                        }
                        if(v_loc_s == "q")return !first;
                        if(v_loc_s == "?"){
                              std::cout << write_mode_help << std::endl;
                              std::cin.clear();
                              goto Write;
                        }
                        if(v_loc_s == "u"){
                              update_mem_map(vmem, integers);
                              std::cin.clear();
                              goto Write;
                        }
                        // TODO: add interactive way to remove locks when there are multiple in place
                        if(v_loc_s == "rl"){
                              if(num_locks == 0)std::cout << "no locks are currently in place" << std::endl;
                              else {
                                    if(integers){
                                          kill(child_pid.first[--num_locks].first, SIGKILL);
                                          std::cout << "lock with value " << child_pid.first[num_locks].second.second << " removed (" << child_pid.first[num_locks].second.first << ")" << std::endl;
                                    }
                                    else{
                                          kill(child_pid.second[--num_locks].first, SIGKILL);
                                          std::cout << "lock with value " << child_pid.second[num_locks].second.second << " removed (" << child_pid.second[num_locks].second.first << ")" << std::endl;
                                    }
                                    wait(NULL);
                              }
                              std::cin.clear();
                              goto Write;
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
                              temp_pid = fork();
                              if(temp_pid == 0){ // TODO: kill this if overwriting the same mem location
                                    bool same = false;
                                    int to_w_i;
                                    //int to_w_i = 0; // to silence -Wmaybe-uninitialized
                                    if(to_w == "_")same = true;
                                    else if(integers)to_w_i = std::stoi(to_w);
                                    // creating pair arrays to store relevant addressses and values so i can free up memory
                                    std::pair<void*, int> vmem_int_subset[v_loc[vl_c]-v_loc[0]+1];
                                    std::pair<void*, std::string> vmem_str_subset[v_loc[vl_c]-v_loc[0]+1];
                                    { // creating a scope to limit c's lifetime
                                          int c = 0;
                                          for(int i = v_loc[0]; i <= v_loc[vl_c]; ++i){
                                                if(integers)vmem_int_subset[c++] = vmem.mmap[i];
                                                else vmem_str_subset[c++] = vmem.cp_mmap[i];
                                          }
                                    }
                                    // this will run for a long time so we might as well free up whatever memory we can
                                    if(integers)delete[] vmem.mmap;
                                    else delete[] vmem.cp_mmap;
                                    while(1){ // child process will forever repeat this
                                          for(int i = 0; i <= v_loc[vl_c]-v_loc[0]; ++i){
                                                if(integers){
                                                      if(same)to_w_i = vmem_int_subset[i].second;
                                                      write_int_to_pid_mem(vmem.pid, vmem_int_subset[i].first, to_w_i);
                                                }
                                                else{
                                                      if(same)to_w = vmem_str_subset[i].second;
                                                      write_str_to_pid_mem(vmem.pid, vmem_str_subset[i].first, to_w);
                                                }
                                          }
                                    }
                              }
                              // TODO: maybe rep both of these with just one <pid_t, <void*, std::string> > and just std::to_string all the ints. this is a little ridiculous
                              //                                                                     because using v_loc[0], it will only show first addr of range
                              if(integers)child_pid.first[num_locks++] = std::make_pair(temp_pid, std::make_pair(vmem.mmap[v_loc[0]].first, std::stoi(to_w)));
                              else child_pid.second[num_locks++] = std::make_pair(temp_pid, std::make_pair(vmem.cp_mmap[v_loc[0]].first, to_w));
                              std::cout << "variable(s) locked" << std::endl;
                              update_mem_map(vmem, integers);
                              //goto Find; // TODO: decide what behavior should be after vars have been locked
                              continue;
                        }
                        for(int i = v_loc[0]; i <= v_loc[vl_c]; ++i){
                              if(integers)write_int_to_pid_mem(vmem.pid, vmem.mmap[i].first, std::stoi(to_w));
                              else{
                                    if(to_w.size() > vmem.cp_mmap[i].second.size()){
                                          std::cout << "WARNING (" << vmem.pid << ":" << vmem.cp_mmap[i].first << "): writing a string that is larger than the original string in its memory location causes undefined behavior" << std::endl; 
                                    }
                                    // TODO: add option to fill up destination string with NUL \0 if to_w.size() < vmem.cp_mmap[i].second.size()
                                    write_str_to_pid_mem(vmem.pid, vmem.cp_mmap[i].first, to_w);
                              }
                        }
                        update_mem_map(vmem, integers); // to make sure accurate values are printed
                  }
            }
            // tmp_str != "w"
            if(tmp_str == "rv"){ // rv for remove volatile for static - disqualifying all volatile vars
                  if(integers){
                        std::cout << remove_volatile_values(vmem) << " volatile variables removed" << std::endl;
                        goto Find;
                  }
            }
            if(first)populate_mem_map(vmem, vmem.pid, d_rgn, additional, integers);
            if(tmp_str == "\\w" || tmp_str == "\\u" || tmp_str == "\\q" || tmp_str == "\\r" || tmp_str == "\\?")tmp_str = tmp_str[1]; // allow searching for 'w' or 'u' with \w or \u
            if(tmp_str == "\\rv" || tmp_str == "\\rl")tmp_str = tmp_str[1] + tmp_str[2];
            if(!first){
                  update_mem_map(vmem, integers);
            }
            if(integers){
                  tmp_val = std::stoi(tmp_str);
                  narrow_mem_map_int(vmem, tmp_val);
            }
            else narrow_mem_map_str(vmem, tmp_str, false);
            if(vmem.size == 0){
                  std::cout << "nothing matches your search of: " << tmp_str << std::endl << "resetting mem map" << std::endl;
                  if(integers)delete[] vmem.mmap;
                  else delete[] vmem.cp_mmap;
                  // setting first to true to imitate behavior of first search and load
                  // reducing space complexity by waiting to repopulate mem_map
                  // TODO: decide whether or not to repopulate immediately - don't think i should so results are accurate to time of search
                  // populate_mem_map(vmem, vmem.pid, d_rgn, additional, integers);
                  // this might introduce an issue where memory isn't fully freed when no results are found
                  first = true;
                  goto Find;
            }
            else{
                  if(!verbose && vmem.size >= result_print_limit){
                        std::cout << "your search of " << tmp_str << " has " << vmem.size << " results\n" << 
                        "result_print_limit is set at " << result_print_limit << ". refusing to print" << std::endl;
                  }
                  else{
                        std::cout << "matches are now:" << std::endl;
                        print_mmap(vmem, "", integers);
                  }
            }
            first = false;
      }
}


int main(int argc, char* argv[]){
      std::string help_str = "NOTE: this program will not work without root privileges\n<pid> {[-p [filter]] [-r <virtual memory address>] [-w <virtual memory addres> <value>] [-i] [-f] [-sb <filename>] [-wb <filename>] [-S] [-H] [-B] [-A] [-E] [-C] [-v]}\n    -p : prints all variables in specified memory region with corresponding virtual memory addresses. optional filter\n    -r : read single value from virtual memory address\n    -w : write single value to virtual memory address\n    -i : inverts all 1s and 0s in specified memory region\n    -f : interactive mode (default)\n    -sb : save backup of process memory to file\n    -wb : restore process memory to backup\n    -S : use stack (default)\n    -H : use heap\n    -B : use both heap and stack\n    -A : look for additional momory regions\n    -E : use all available memory regions\n    -C : use char/string mode\n    -v : verbose mode (only affects interactive mode and restore backup)\n";

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
      pid_t pid = (pid_t)std::stoi(argv[1]);
      // initializing here extends scope to default behavior to avoid rescanning memory
      mem_map vmem;
      // vmem.mapped_rgn = get_vmem_locations(pid, true);
      // TODO: fix criteria for unmarked additional mem rgns in vmem_parser.cpp
      vmem.mapped_rgn = get_vmem_locations(pid, false); // disabling unmarked additional rgns until criteria for unmarked additional mem rgns are fixed
      if(argc > 2){
            // -r and -w can be done without slowly loading a complete mem_map
            if(strcmp(argv[2], "-r") == 0){
                  if(integers)std::cout << read_single_val_from_pid_mem(pid, 4, (void*)strtoul(argv[3], 0, 16)) << std::endl;
                  else std::cout << read_str_from_mem_block_slow(pid, (void*)strtoul(argv[3], 0, 16), nullptr) << std::endl;
                  return 0;
            }
            if(strcmp(argv[2], "-w") == 0){
                  if(integers)write_int_to_pid_mem(pid, (void*)strtoul(argv[3], 0, 16), std::stoi(argv[4]));
                  else write_str_to_pid_mem(pid, (void*)strtoul(argv[3], 0, 16), argv[4]);
                  return 0;
            }
            if(strcmp(argv[2], "-wb") == 0){
                  restore_pid_mem_state(pid, argv[3], verbose);
                  return 0;
            }
            if(!mem_rgn_warn(d_rgn, vmem.mapped_rgn, additional))return -1;
            // stop here if none of our required data regions are available
            if(strcmp(argv[2], "-sb") == 0){
                  populate_mem_map(vmem, pid, d_rgn, additional, true);
                  save_pid_mem_state(vmem, argv[3]);
                  delete[] vmem.mmap;
                  return 0;
            }
            if(strcmp(argv[2], "-f") == 0){
                  SAFE_INTER:
                  vmem.pid = pid;
                  if(interactive_mode(vmem, integers, d_rgn, additional, verbose)){
                        if(integers)delete[] vmem.mmap;
                        else delete[] vmem.cp_mmap;
                        delete[] vmem.mapped_rgn.remaining_addr;
                  }
                  return 0;
            }
            if(strcmp(argv[2], "-p") == 0){
                  populate_mem_map(vmem, pid, d_rgn, additional, integers);
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
                  populate_mem_map(vmem, pid, d_rgn, additional, integers);
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
            goto SAFE_INTER; // go back to interactive mode before mem_rgn_warn is called again
      }
      // argc <= 2
      if(!mem_rgn_warn(d_rgn, vmem.mapped_rgn, additional))return -1;
      // default to interactive mode
      goto SAFE_INTER;
}
