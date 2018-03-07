#include <string.h>
#include <unistd.h> // fork

#include <signal.h> // kill()
#include <sys/wait.h> // wait()

#include "vmem_access.h"

bool valid_int(const char* str){
      char* res;
      strtol(str, &res, 10);
      return *res == 0;
}

bool mem_rgn_warn(int d_rgn, struct mem_rgn mem, bool additional){
      bool no_ad = (mem.n_remaining == 0 && additional);
      bool stack = true;
      if(no_ad)printf("WARNING: no valid unmarked memory regions were found\n");
      if((d_rgn == STACK || d_rgn == BOTH) && (mem.stack_start_addr == NULL || mem.stack_end_addr == NULL)){
            printf("WARNING: no valid stack memory region was found\n");
            if(d_rgn == STACK && (no_ad || !additional))return false;
            stack = false;
      }
      if((d_rgn == HEAP || d_rgn == BOTH) && (mem.heap_start_addr == NULL || mem.heap_end_addr == NULL)){
            printf("WARNING: no valid heap memory region was found\n");
            if((d_rgn == HEAP || (d_rgn == BOTH && !stack)) && (no_ad || !additional))return false;
      }
      return true;
}

// TODO trans this
/*
 *void save_pid_mem_state(const mem_map &vmem, std::string outf){
 *      std::ofstream ofs(outf);
 *      for(int i = 0; i < vmem->size; ++i){
 *            ofs << vmem->mmap[i].first << " " << vmem->mmap[i].second << "\n";
 *      }
 *      ofs.close();
 *}
 *
 *void restore_pid_mem_state(pid_t pid, std::string inf, bool verbose){
 *      std::ifstream ifs(inf);
 *      std::string tmp_addr;
 *      int tmp_i;
 *      while(ifs >> tmp_addr >> tmp_i){
 *            if(tmp_addr != "0"){
 *            //if(tmp_addr != "0" && tmp_i != 0 && tmp_i != 1){ // TODO: should i write 1's and 0's?
 *                  if(verbose){
 *                        if(tmp_i != read_single_val_from_pid_mem(pid, 4, (void*)strtoul(tmp_addr.c_str(), 0, 16))){
 *                              printf("%s: %i -> %i\n", tmp_addr.c_str(), read_single_val_from_pid_mem(pid, 4, (void*)strtoul(tmp_addr.c_str(), 0, 16)), tmp_i);
 *                        }
 *                  }
 *                  write_int_to_pid_mem(pid, (void*)strtoul(tmp_addr.c_str(), 0, 16), tmp_i);
 *            }
 *      }
 *      ifs.close();
 *}
 */

int remove_volatile_values(struct mem_map* vmem){
      int n = 0;
      for(int i = 0; i < vmem->size; ++i){
            for(int in = 0; in < 10; ++in){
                  if(vmem->mmap[i].second != read_single_val_from_pid_mem(vmem->pid, 4, vmem->mmap[i].first)){
                        vmem->mmap[i--] = vmem->mmap[--vmem->size];
                        ++n;
                  }
            }
      }
      return n;
}

void print_mmap(const struct mem_map* mem, const char* contains, bool integers, bool show_rgns){
      char tmp_num[20];
      for(int i = 0; i < mem->size; ++i){
            if(integers){
                  sprintf(tmp_num, "%d", mem->mmap[i].second);
                  if(strcmp(contains, "") == 0 || strcmp(tmp_num, contains) == 0){
                        if(show_rgns)printf("%p (%s) : %i\n", mem->mmap[i].first, which_rgn(mem->mapped_rgn, mem->mmap[i].first), mem->mmap[i].second);
                        else printf("%p: %i\n", mem->mmap[i].first, mem->mmap[i].second);
                  }
            }
            else{
                  if(strcmp(contains, "") == 0 || is_substr(contains, mem->cp_mmap[i].second)){
                        if(show_rgns)printf("%p (%s) : %s\n", mem->cp_mmap[i].first, which_rgn(mem->mapped_rgn, mem->cp_mmap[i].first), mem->cp_mmap[i].second);
                        else printf("%p: %s\n", mem->cp_mmap[i].first, mem->cp_mmap[i].second);
                  }
            }
      }
}

// param can be const ref because i'm just using the mem_map as a guide for which mem locations to invert
void logic_swap(const struct mem_map* mem){
      for(int i = 0; i < mem->size; ++i){
            if(mem->mmap[i].second == 0 || mem->mmap[i].second == 1)
            write_int_to_pid_mem(mem->pid, mem->mmap[i].first, (int)!mem->mmap[i].second);
      }
}

bool interactive_mode(struct mem_map* vmem, bool integers, int int_mode_bytes, int d_rgn, int additional, bool verbose, int result_print_limit, bool print_rgns){
      char search_mode_help[600];
      strcpy(search_mode_help, "search mode options:\n    'r' : reset mem map\n    \"wa\" <value> : write single value to all current results\n    ");
      if(integers)strcpy(search_mode_help, "<integer> : enter an integer to narrow results\n    \"rv\" : remove volatile variables\n    ");
      else strcpy(search_mode_help, "<string> : enter a string to narrow results - use delimeter '\\' to search for '?', 'q', 'u', 'r', \"rl\", 'w'\n    ");
      strcpy(search_mode_help, "'u' : update visible values\n    \"rl\" : remove most recently applied lock\n    '?' : show this\n    'q' : quit");
      char write_mode_help[480] = "NOTE: <memory location reference #> can be replaced with <start reference #>-<end reference #>\nwrite mode options:\n    <memory location reference #> <value to write> : writes value to memory location(s)\n    l <memory location reference #> <value to write> : locks memory location(s) to provided value\n    l <memory location reference #> _ : locks memory location(s) to their current value(s)\n    \"rl\" : remove most recently applied lock\n    '?' : show this\n    'q' : quit";
      printf("in interactive mode on process %i (%s)\nusing ", vmem->pid, vmem->mapped_rgn.p_name);
      if(d_rgn == STACK)printf("stack");
      if(d_rgn == HEAP)printf("heap");
      if(d_rgn == BOTH)printf("both stack and heap");
      if(additional && vmem->mapped_rgn.n_remaining != 0)printf(" as well as %i additional memory regions", vmem->mapped_rgn.n_remaining);
      printf(" - looking for ");
      if(integers)printf("integers\n");
      else printf("strings\n");
      printf("enter 'u' at any time to update visible values, 'q' to exit or '?' for help\n");
      // tmp_str needs to be large enough for any search string
      // TODO: read in chunks to assure large enough string/not to overuse memory or use getline
      char tmp_str[4096];
      int tmp_val;
      bool first = true;
      bool lock_mode;
      // the three variables below are used to keep track of var locks
      struct lock_container child_pid[30];
      pid_t temp_pid;
      int num_locks = 0;
      while(1){
            Find:
            printf("enter current variable value to search");
            if(!first)printf(" or 'w' to enter write mode");
            printf("\n");
            //std::cin >> tmp_str;
            // too small to search for strings
            /*fgets(tmp_str, 20, stdin);*/
            fgets(tmp_str, 4096, stdin);
            tmp_str[strlen(tmp_str)-1]='\0';
            if(strcmp(tmp_str, "q") == 0)return !first;
            if(strcmp(tmp_str, "?") == 0){
                  printf("%s\n", search_mode_help);
                  goto Find;
            }
            if(strcmp(tmp_str, "u") == 0){
                  update_mem_map(vmem, integers);
                  //fseek(stdin, 0, SEEK_END);
                  //std::cin.clear();
                  fseek(stdin, 0, SEEK_END);
                  print_mmap(vmem, "", integers, print_rgns);
                  goto Find;
            }
            if(strcmp(tmp_str, "r") == 0){
                  if(vmem->size != 0){
                        if(integers)free(vmem->mmap);
                        else free(vmem->cp_mmap);
                        vmem->size = 0;
                        first = true;
                  }
                  printf("mem map has been reset\n");
                  fseek(stdin, 0, SEEK_END);
                  goto Find;
            }
            // TODO: decide if i want to allow removal of locks in search mode
            if(strcmp(tmp_str, "rl") == 0){
                  if(num_locks == 0)printf("no locks are currently in place\n");
                  else {
                        printf("killing %i\n", child_pid[num_locks-1].pid);
                        kill(child_pid[--num_locks].pid, SIGKILL);
                        // TODO possibly let user know that we're using freeze/same mode
                        // TODO possibly let user know that we're printing the first of a range
                        //if(child_pid[num_locks].second.second == "_")
                        if(integers)printf("lock with value %i removed (%p)\n", child_pid[num_locks].i_value, child_pid[num_locks].m_addr);
                        else printf("lock with value \"%s\" removed (%p)\n", child_pid[num_locks].s_value, child_pid[num_locks].m_addr);
                        wait(NULL);
                  }
                  fseek(stdin, 0, SEEK_END);
                  goto Find;
            }
            // wa mode
            if(tmp_str[0] == 'w' && tmp_str[1] == 'a'){
                  char val[18]; int v_s = 0;
                  bool in_str = false;
                  for(unsigned int s = 2; s < strlen(tmp_str); ++s){
                        if(tmp_str[s] != ' '){
                              in_str = true;
                              val[v_s++] = tmp_str[s];
                        }
                        else if(in_str)break;
                  }
                  // get rid of possible initialized values
                  memset(val+v_s, '\0', sizeof(char)*(18-v_s));
                  if(integers)tmp_val = atoi(val);
                  for(int i = 0; i < vmem->size; ++i){
                        if(integers)write_int_to_pid_mem(vmem->pid, vmem->mmap[i].first, tmp_val);
                        else write_str_to_pid_mem(vmem->pid, vmem->cp_mmap[i].first, val);
                  }
                  printf("wrote %s to %li memory locations\n", val, vmem->size);
                  goto Find;
            }
            if(strcmp(tmp_str, "w") == 0){
                  int vl_c;
                  // to_w needs to be large enough to store any write string
                  // TODO: make to_w char* and use getline()
                  char tmp_num[20], v_loc_s[10], to_w[4096];
                  int tmp_num_p = 0;
                  int v_loc[2]; // v_loc stores start and end of a range
                  while(1){
                        Write:
                        if(first){
                              printf("no memory locations available for writing. returning to search\n");
                              goto Find;
                        }
                        if(integers){
                              for(int i = 0; i < vmem->size; ++i){
                                    printf("%i: (%p: %i)\n", i, vmem->mmap[i].first, vmem->mmap[i].second);
                              }
                        }
                        else{
                              for(int i = 0; i < vmem->size; ++i){
                                    printf("%i: (%p: \"%s\")\n", i, vmem->cp_mmap[i].first, vmem->cp_mmap[i].second);
                              }
                        }
                        printf("enter a number from [0-%li] or a range with a '-', followed by a value to write OR 's' to continue searching\n", vmem->size-1);
                        // width is 1 less than length of length of v_loc_s to avoid overwriting '\0'
                        scanf(" %9[^ \t.\n]%*c", v_loc_s);
                        //v_loc_s[strlen(v_loc_s)-1]='\0';
                        if(strcmp(v_loc_s, "s") == 0){
                              fseek(stdin, 0, SEEK_END);
                              print_mmap(vmem, "", integers, print_rgns);
                              goto Find;
                        }
                        if(strcmp(v_loc_s, "q") == 0)return !first;
                        if(strcmp(v_loc_s, "?") == 0){
                              printf("%s", write_mode_help);
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        if(strcmp(v_loc_s, "u") == 0){
                              update_mem_map(vmem, integers);
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        // TODO: add interactive way to remove locks when there are multiple in place
                        if(strcmp(v_loc_s, "rl") == 0){
                              if(num_locks == 0)printf("no locks are currently in place\n");
                              else {
                                    kill(child_pid[--num_locks].pid, SIGKILL);
                                    if(integers)printf("lock with value %i removed (%p)\n", child_pid[num_locks].i_value, child_pid[num_locks].m_addr);
                                    else printf("lock with value \"%s\" removed (%p)\n", child_pid[num_locks].s_value, child_pid[num_locks].m_addr);
                                    wait(NULL);
                              }
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        lock_mode = false;
                        if(strcmp(v_loc_s, "l") == 0){
                              lock_mode = true;
                              //std::cin >> v_loc_s;
                              //fgets(v_loc_s, 10, stdin);
                              scanf(" %9[^ \t.\n]%*c", v_loc_s);
                              //v_loc_s[strlen(v_loc_s)-1]='\0';
                        }
                        // std::ws to get rid of leading whitespace
                        //std::getline(std::cin >> std::ws, to_w); // TODO: maybe learn how to ignore leading ws
                        scanf(" %4095[^ \t.\n]%*c", to_w);
                        /*
                         *fgets(to_w, 20, stdin);
                         * { // to limit scope of s
                         *unsigned int s = 0;
                         *for(; s < strlen(to_w); ++s){
                         *      if(to_w[s] != ' ' && to_w[s] != '\n')break;
                         * }
                         * // my attempt to get rid of whitespace - fix this up by clearing extra string demonstrated below
                         * strcpy(to_w, to_w+s);
                         * //memset(to_w
                         * //"  aas" s = 1
                         * //"aasas" 
                         *}
                         */
                        //to_w[strlen(to_w)-1] = '\0';
                        if(integers && !valid_int(to_w) && !(lock_mode && strcmp(to_w, "_") == 0)){
                              printf("enter a valid integer to write\n");
                              goto Write;
                        }
                        vl_c = 0;
                        memset(tmp_num, '\0', sizeof(char)*20);
                        tmp_num_p = 0;
                        bool found_d = false;
                        for(unsigned int i = 0; i < strlen(v_loc_s); ++i){
                              if(v_loc_s[i] == '-'){
                                    found_d = true;
                                    // checking first int of range
                                    if(!valid_int(tmp_num)){
                                          printf("enter a valid integer or range of integers\n");
                                          goto Write;
                                    }
                                    v_loc[vl_c++] = atoi(tmp_num);
                                    memset(tmp_num, '\0', sizeof(char)*20);
                              }
                              else{
                                    if(found_d)strcpy(tmp_num, v_loc_s+i);
                                    else tmp_num[tmp_num_p++] = v_loc_s[i];
                              }
                        }
                        // checking second int of range
                        if(!valid_int(tmp_num)){
                              printf("enter a valid integer or range of integers\n");
                              goto Write;
                        }
                        v_loc[vl_c] = atoi(tmp_num);
                        if(lock_mode){
                              temp_pid = fork();
                              if(temp_pid == 0){ // TODO: kill this if overwriting the same mem location
                                    bool same = false;
                                    int to_w_i;
                                    //int to_w_i = 0; // to silence -Wmaybe-uninitialized
                                    if(strcmp(to_w, "_") == 0)same = true;
                                    else if(integers)to_w_i = atoi(to_w);
                                    // creating pair arrays to store relevant addresses and values so i can free up memory
                                    struct addr_int_pair vmem_int_subset [v_loc[vl_c]-v_loc[0]+1];
                                    struct addr_str_pair vmem_str_subset[v_loc[vl_c]-v_loc[0]+1];
                                    { // creating a scope to limit c's lifetime
                                          int c = 0;
                                          for(int i = v_loc[0]; i <= v_loc[vl_c]; ++i){
                                                if(integers)vmem_int_subset[c++] = vmem->mmap[i];
                                                else vmem_str_subset[c++] = vmem->cp_mmap[i];
                                          }
                                    }
                                    // this will run for a long time so we might as well free up whatever memory we can
                                    free_mem_map(vmem, integers);
                                    while(1){ // child process will forever repeat this
                                          for(int i = 0; i <= v_loc[vl_c]-v_loc[0]; ++i){
                                                if(integers){
                                                      if(same)to_w_i = vmem_int_subset[i].second;
                                                      write_int_to_pid_mem(vmem->pid, vmem_int_subset[i].first, to_w_i);
                                                }
                                                else{
                                                      //if(same)to_w = vmem_str_subset[i].second;
                                                      if(same)strcpy(to_w, vmem_str_subset[i].second);
                                                      write_str_to_pid_mem(vmem->pid, vmem_str_subset[i].first, to_w);
                                                }
                                          }
                                    }
                              }
                              // TODO: add show/print lock mode
                              // writing raw string to child_pid regardless of string/int mode - this avoids the need to handle strings separately from ints
                              ++num_locks;
                              child_pid[num_locks-1].s_value = to_w;
                              child_pid[num_locks-1].pid = temp_pid;
                              if(integers){
                                    child_pid[num_locks-1].m_addr = vmem->mmap[v_loc[0]].first;
                                    // if we're locking values using "_" notation don't try atoi
                                    if(strcmp(to_w, "_") != 0){
                                          child_pid[num_locks-1].i_value = atoi(to_w);
                                    }
                                    else child_pid[num_locks-1].i_value = vmem->mmap[v_loc[0]].second;
                              }
                              else child_pid[num_locks-1].m_addr = vmem->cp_mmap[v_loc[0]].first;
                              printf("variable(s) locked\n");
                              update_mem_map(vmem, integers);
                              //goto Find; // TODO: decide what behavior should be after vars have been locked
                              continue;
                        }
                        for(int i = v_loc[0]; i <= v_loc[vl_c]; ++i){
                              if(integers)write_int_to_pid_mem(vmem->pid, vmem->mmap[i].first, atoi(to_w));
                              else{
                                    if(strlen(to_w) > strlen(vmem->cp_mmap[i].second)){
                                          printf("WARNING (%i: %p): writing a string that is larger than the original string in its memory location causes undefined behavior\n", vmem->pid, vmem->cp_mmap[i].first);
                                    }
                                    // TODO: add option to fill up destination string with NUL \0 if to_w.size() < vmem->cp_mmap[i].second.size()
                                    write_str_to_pid_mem(vmem->pid, vmem->cp_mmap[i].first, to_w);
                              }
                        }
                        update_mem_map(vmem, integers); // to make sure accurate values are printed
                  }
            }
            // tmp_str != "w"
            if(strcmp(tmp_str, "rv") == 0){ // rv for remove volatile for static - disqualifying all volatile vars
                  if(integers){
                        printf("%i volatile variables removed\n", remove_volatile_values(vmem));
                        goto Find;
                  }
            }
            // checking if input is valid integer before populating mem_map
            //tmp_str[strlen(tmp_str)-1] = '\0';
            if((strcmp(tmp_str, "") == 0 || !valid_int(tmp_str)) && integers){
                  printf("enter a valid integer to search\n");
                  goto Find;
            }
            else if(strcmp(tmp_str, "") == 0){
                  printf("enter a valid string to search\n");
                  goto Find;
            }
            if(first)populate_mem_map(vmem, vmem->pid, d_rgn, additional, integers, int_mode_bytes);
            if(strcmp(tmp_str, "\\w") == 0 || strcmp(tmp_str, "\\u") == 0 || strcmp(tmp_str, "\\q") == 0 || strcmp(tmp_str, "\\r") == 0 || strcmp(tmp_str, "\\?") == 0){
                  tmp_str[0] = tmp_str[1]; // allow searching for 'w' or 'u' with \w or \u
                  tmp_str[1] = '\0';
            }
            if(strcmp(tmp_str, "\\rv") == 0 || strcmp(tmp_str, "\\rl") == 0){
                  //tmp_str = tmp_str[1] + tmp_str[2];
                  tmp_str[0] = tmp_str[1];
                  tmp_str[1] = tmp_str[2];
                  tmp_str[2] = '\0';
            }
            if(!first)update_mem_map(vmem, integers);
            if(integers){
                  tmp_val = atoi(tmp_str);
                  narrow_mem_map_int(vmem, tmp_val);
            }
            else narrow_mem_map_str(vmem, tmp_str, false);
            if(vmem->size == 0){
                  printf("nothing matches your search of: %s\nresetting mem map\n", tmp_str);
                  if(integers)free(vmem->mmap);
                  else free(vmem->cp_mmap);
                  // setting first to true to imitate behavior of first search and load, reducing space complexity by waiting to repopulate mem_map
                  // TODO: decide whether or not to repopulate immediately - don't think i should so results are accurate to time of search
                  // populate_mem_map(vmem, vmem->pid, d_rgn, additional, integers);
                  // this might introduce an issue where memory isn't fully freed when no results are found
                  first = true;
                  goto Find;
            }
            else{
                  if(!verbose && vmem->size > result_print_limit){
                        printf("your search of %s has %li results\nresult_print_limit is set at %i. refusing to print\n", tmp_str, vmem->size, result_print_limit);
                  }
                  else{
                        printf("matches are now:\n");
                        print_mmap(vmem, "", integers, print_rgns);
                  }
            }
            first = false;
      }
}


int main(int argc, char* argv[]){
      char help_str[1185] = "NOTE: this program will not work without root privileges\n<pid> {[-p [filter]] [-r <virtual memory address>] [-w <virtual memory addres> <value>] [-i] [-f] [-sb <filename>] [-wb <filename>] [-S] [-H] [-B] [-A] [-E] [-C] [-b <integer>] [-v] [-pr] [-pl <print limit>]}\n    -p : prints all variables in specified memory region with corresponding virtual memory addresses. optional filter\n    -r : read single value from virtual memory address\n    -w : write single value to virtual memory address\n    -i : inverts all 1s and 0s in specified memory region\n    -f : interactive mode (default)\n    -sb : save backup of process memory to file\n    -wb : restore process memory to backup\n    -S : use stack (default)\n    -H : use heap\n    -B : use both heap and stack\n    -A : look for additional momory regions\n    -E : use all available memory regions\n    -C : use char/string mode\n    -b : set number of bytes to read at a time in integer mode\n    -v : verbose mode (enables print region mode)\n    -pr : print region that memory addresses are found in\n    -pl : set print limit for search results (only affects interactive mode, can be useful for small screens)\n";

      if(argc == 1 || (argc > 1 && strcmp(argv[1], "-h") == 0)){
            printf("%s", help_str);
            return -1;
      }
      bool integers = true, additional=false, verbose=false, print_rgns=false;
      // TODO: initialize d_rgn to NONE and handle that case
      int d_rgn = STACK, n_bytes=4, result_print_limit=100;
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
            if(strcmp(argv[i], "-b") == 0){
                  n_bytes = atoi(argv[i+1]);
            }
            if(strcmp(argv[i], "-v") == 0){
                  verbose = true;
                  print_rgns = true;
            }
            if(strcmp(argv[i], "-pr") == 0){
                  print_rgns = true;
            }
            // print limit only has an effect on interactive mode
            if(strcmp(argv[i], "-pl") == 0){
                 result_print_limit = atoi(argv[i+1]);
            }
      }
      pid_t pid = (pid_t)atoi(argv[1]);
      // initializing here extends scope to default behavior to avoid rescanning memory
      struct mem_map vmem;
      vmem.size = 0;
      // vmem->mapped_rgn = get_vmem_locations(pid, true);
      // TODO: fix criteria for unmarked additional mem rgns in vmem_parser.cpp
      vmem.mapped_rgn = get_vmem_locations(pid, false); // disabling unmarked additional rgns until criteria for unmarked additional mem rgns are fixed
      if(!mem_rgn_warn(d_rgn, vmem.mapped_rgn, additional))return -1;
      if(argc > 2){
            // -r and -w can be done without slowly loading a complete mem_map
            if(strcmp(argv[2], "-r") == 0){
                  if(integers)printf("%i\n", read_single_val_from_pid_mem(pid, n_bytes, (void*)strtoul(argv[3], 0, 16)));
                  else printf("%s\n", read_str_from_mem_block_slow(pid, (void*)strtoul(argv[3], 0, 16), NULL));
                  return 0;
            }
            if(strcmp(argv[2], "-w") == 0){
                  if(integers)write_int_to_pid_mem(pid, (void*)strtoul(argv[3], 0, 16), atoi(argv[4]));
                  else write_str_to_pid_mem(pid, (void*)strtoul(argv[3], 0, 16), argv[4]);
                  return 0;
            }
            /*
             *if(strcmp(argv[2], "-wb") == 0){
             *      restore_pid_mem_state(pid, argv[3], verbose);
             *      return 0;
             *}
             *if(!mem_rgn_warn(d_rgn, vmem->mapped_rgn, additional))return -1;
             * // stop here if none of our required data regions are available
             *if(strcmp(argv[2], "-sb") == 0){
             *      populate_mem_map(vmem, pid, d_rgn, additional, true);
             *      save_pid_mem_state(vmem, argv[3]);
             *      delete[] vmem->mmap;
             *      delete[] vmem->mapped_rgn.remaining_addr;
             *      return 0;
             *}
             */
            if(strcmp(argv[2], "-f") == 0){
                  SAFE_INTER:
                  vmem.pid = pid;
                  if(interactive_mode(&vmem, integers, n_bytes, d_rgn, additional, verbose, result_print_limit, print_rgns)){
                        free_mem_map(&vmem, integers);
                  }
                  return 0;
            }
            if(strcmp(argv[2], "-p") == 0){
                  populate_mem_map(&vmem, pid, d_rgn, additional, integers, n_bytes);
                  if(argc > 3 && argv[3][0] != '-'){
                        print_mmap(&vmem, argv[3], integers, print_rgns);
                  }
                  else print_mmap(&vmem, "", integers, print_rgns);
                  free_mem_map(&vmem, integers);
                  return 0;
            }
            if(strcmp(argv[2], "-i") == 0){
                  if(!integers){
                        printf("cannot invert string/char*\n");
                        free_mem_map(&vmem, false);
                        return -1;
                  }
                  populate_mem_map(&vmem, pid, d_rgn, additional, integers, 1);
                  logic_swap(&vmem);
                  free_mem_map(&vmem, integers);
                  return 0;
            }
            goto SAFE_INTER;
      }
      // argc <= 2
      // default to interactive mode
      goto SAFE_INTER;
}
