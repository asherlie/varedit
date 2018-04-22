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
      if(no_ad)puts("WARNING: no valid unmarked memory regions were found");
      if((d_rgn == STACK || d_rgn == BOTH) && (mem.stack_start_addr == NULL || mem.stack_end_addr == NULL)){
            puts("WARNING: no valid stack memory region was found");
            if(d_rgn == STACK && (no_ad || !additional))return false;
            stack = false;
      }
      if((d_rgn == HEAP || d_rgn == BOTH) && (mem.heap_start_addr == NULL || mem.heap_end_addr == NULL)){
            puts("WARNING: no valid heap memory region was found");
            if((d_rgn == HEAP || (d_rgn == BOTH && !stack)) && (no_ad || !additional))return false;
      }
      return true;
}

int remove_volatile_values(struct mem_map* vmem){
      int n = 0;
      for(unsigned long i = 0; i < vmem->size; ++i){
            for(int in = 0; in < 10; ++in){
                  if(vmem->mmap[i].value != read_single_val_from_pid_mem(vmem->pid, 4, vmem->mmap[i].addr)){
                        vmem->mmap[i--] = vmem->mmap[--vmem->size];
                        ++n;
                  }
            }
      }
      return n;
}

void print_mmap(const struct mem_map* mem, const char* contains, bool integers, bool show_rgns){
      char tmp_num[20];
      for(unsigned long i = 0; i < mem->size; ++i){
            if(integers){
                  sprintf(tmp_num, "%d", mem->mmap[i].value);
                  if(strcmp(contains, "") == 0 || strcmp(tmp_num, contains) == 0){
                        if(show_rgns)printf("%p (%s) : %i\n", mem->mmap[i].addr, which_rgn(mem->mapped_rgn, mem->mmap[i].addr), mem->mmap[i].value);
                        else printf("%p: %i\n", mem->mmap[i].addr, mem->mmap[i].value);
                  }
            }
            else{
                  if(strcmp(contains, "") == 0 || is_substr(contains, mem->cp_mmap[i].value)){
                        if(show_rgns)printf("%p (%s) : \"%s\"\n", mem->cp_mmap[i].addr, which_rgn(mem->mapped_rgn, mem->cp_mmap[i].addr), mem->cp_mmap[i].value);
                        else printf("%p: \"%s\"\n", mem->cp_mmap[i].addr, mem->cp_mmap[i].value);
                  }
            }
      }
}

void logic_swap(const struct mem_map* mem){
      for(unsigned long i = 0; i < mem->size; ++i){
            if(mem->mmap[i].value == 0 || mem->mmap[i].value == 1)
            write_bytes_to_pid_mem(mem->pid, 1, mem->mmap[i].addr, (unsigned char*)&mem->mmap[i].value);
      }
}

bool interactive_mode(struct mem_map* vmem, bool integers, int int_mode_bytes, int d_rgn, int additional, bool verbose, unsigned long result_print_limit, bool print_rgns){
      char search_mode_help[600];
      strcpy(search_mode_help, "search mode options:\n    'r' : reset mem map\n    \"wa\" <value> : write single value to all current results\n    ");
      if(integers)strcpy(search_mode_help+110, "<integer> : enter an integer to narrow results\n    \"rv\" : remove volatile variables\n    ");
      else strcpy(search_mode_help+110, "<string> : enter a string to narrow results - use delimeter '\\' to search for '?', 'q', 'u', 'r', \"rl\", 'w'\n    ");
      strcpy(search_mode_help+strlen(search_mode_help), "'u' : update visible values\n    \"rl\" : remove most recently applied lock\n    '?' : show this\n    'q' : quit");
      char write_mode_help[] = "NOTE: <memory location reference #> can be replaced with <start reference #>-<end reference #>\nwrite mode options:\n    <memory location reference #> <value to write> : writes value to memory location(s)\n    l <memory location reference #> <value to write> : locks memory location(s) to provided value\n    l <memory location reference #> _ : locks memory location(s) to their current value(s)\n    \"rl\" : remove most recently applied lock\n    '?' : show this\n    'q' : quit";
      printf("in interactive mode on process %i (%s)\nusing ", vmem->pid, vmem->mapped_rgn.p_name);
      if(d_rgn == STACK)printf("stack");
      if(d_rgn == HEAP)printf("heap");
      if(d_rgn == BOTH)printf("both stack and heap");
      if(additional && vmem->mapped_rgn.n_remaining != 0)printf(" as well as %i additional memory regions", vmem->mapped_rgn.n_remaining);
      printf(" - looking for ");
      if(integers)puts("integers");
      else puts("strings");
      puts("enter 'u' at any time to update visible values, 'q' to exit or '?' for help");
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
      unsigned short tmp_strlen = 0;
      while(1){
            Find:
            printf("enter current variable value to search");
            if(!first)printf(" or 'w' to enter write mode");
            puts("");
            fgets(tmp_str, 4096, stdin);
            tmp_strlen = strlen(tmp_str);
            tmp_str[tmp_strlen-1]='\0';
            if(strcmp(tmp_str, "q") == 0)return !first;
            if(strcmp(tmp_str, "?") == 0){
                  printf("%s\n", search_mode_help);
                  goto Find;
            }
            if(strcmp(tmp_str, "u") == 0){
                  update_mem_map(vmem, integers);
                  fseek(stdin, 0, SEEK_END);
                  print_mmap(vmem, "", integers, print_rgns);
                  goto Find;
            }
            if(strcmp(tmp_str, "r") == 0){
                  if(vmem->size != 0){
                        free_mem_map(vmem, integers);
                        vmem->size = 0;
                        first = true;
                  }
                  puts("mem map has been reset");
                  fseek(stdin, 0, SEEK_END);
                  goto Find;
            }
            // TODO: decide if i want to allow removal of locks in search mode
            if(strcmp(tmp_str, "rl") == 0){
                  if(num_locks == 0)puts("no locks are currently in place");
                  else {
                        printf("killing %i\n", child_pid[num_locks-1].pid);
                        kill(child_pid[--num_locks].pid, SIGKILL);
                        // TODO possibly let user know that we're using freeze/same mode
                        // TODO possibly let user know that we're printing the first of a range
                        if(integers)printf("lock with value %i removed (%p)\n", child_pid[num_locks].i_value, child_pid[num_locks].m_addr);
                        else printf("lock with value \"%s\" removed (%p)\n", child_pid[num_locks].s_value, child_pid[num_locks].m_addr);
                        wait(NULL);
                  }
                  fseek(stdin, 0, SEEK_END);
                  goto Find;
            }
            // wa mode
            if(tmp_strlen > 3){
                  if(tmp_str[0] == 'w' && tmp_str[1] == 'a' && tmp_str[2] == ' '){
                        // for int mode
                        BYTE write[int_mode_bytes];
                        if(integers){
                              tmp_val = atoi(tmp_str+3);
                              memcpy(write, &tmp_val, int_mode_bytes);
                        }
                        for(unsigned long i = 0; i < vmem->size; ++i){
                              if(integers)write_bytes_to_pid_mem(vmem->pid, int_mode_bytes, vmem->mmap[i].addr, write);
                              else write_str_to_pid_mem(vmem->pid, vmem->cp_mmap[i].addr, tmp_str+3);
                        }
                        printf("wrote \"%s\" to %li memory locations\n", tmp_str+3, vmem->size);
                        goto Find;
                  }
            }
            if(strcmp(tmp_str, "w") == 0){
                  // to_w needs to be large enough to store any write string
                  // TODO: make to_w char* and use getline()
                  char v_loc_s[10], to_w[4096];
                  int v_loc[2]; // v_loc stores start and end of range
                  unsigned short to_w_len = 0;
                  while(1){
                        Write:
                        if(first){
                              puts("no memory locations available for writing. returning to search");
                              goto Find;
                        }
                        if(integers){
                              for(unsigned long i = 0; i < vmem->size; ++i){
                                    printf("%li: (%p: %i)\n", i, vmem->mmap[i].addr, vmem->mmap[i].value);
                              }
                        }
                        else{
                              for(unsigned long i = 0; i < vmem->size; ++i){
                                    printf("%li: (%p: \"%s\")\n", i, vmem->cp_mmap[i].addr, vmem->cp_mmap[i].value);
                              }
                        }
                        printf("enter a number from [0-%li] or a range with a '-', followed by a value to write OR 's' to continue searching\n", vmem->size-1);
                        // width is 1 less than length of length of v_loc_s to avoid overwriting '\0'
                        // ignore leading whitespace
                        scanf(" %9[^ \t.\n]%*c", v_loc_s);
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
                              if(num_locks == 0)puts("no locks are currently in place");
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
                              scanf(" %9[^ \t.\n]%*c", v_loc_s);
                        }
                        scanf("%4095[^\t.\n]%*c", to_w);
                        if(integers && !valid_int(to_w) && !(lock_mode && strcmp(to_w, "_") == 0)){
                              puts("enter a valid integer to write");
                              goto Write;
                        }
                        to_w_len = strlen(to_w);
                        { // scope to limit e_r's lifetime
                              char* e_r;
                              e_r = strchr(v_loc_s, '-');
                              if(e_r != NULL)*(e_r++) = '\0';
                              // setting both indices in case not range
                              if(valid_int(v_loc_s))v_loc[1] = v_loc[0] = atoi(v_loc_s);
                              else{
                                    Int_err:
                                    puts("enter a valid integer or range of integers");
                                    goto Write;
                              }
                              if(e_r != NULL){
                                    if(!valid_int(e_r))goto Int_err;
                                    v_loc[1] = atoi(e_r);
                              }
                              // TODO: switch mem_map.size to uint, ulong is overkill
                              if(v_loc[0] >= (int)vmem->size || v_loc[1] > (int)vmem->size)goto Int_err;
                        }
                        if(lock_mode){
                              temp_pid = fork();
                              if(temp_pid == 0){ // TODO: kill this if overwriting the same mem location
                                    bool same = false;
                                    int to_w_i;
                                    if(strcmp(to_w, "_") == 0)same = true;
                                    else if(integers)to_w_i = atoi(to_w);
                                    // creating pair arrays to store relevant addresses and values so i can free up memory
                                    struct addr_int_pair vmem_int_subset [v_loc[1]-v_loc[0]+1];
                                    struct addr_str_pair vmem_str_subset[v_loc[1]-v_loc[0]+1];
                                    { // creating a scope to limit c's lifetime
                                          int c = 0;
                                          for(int i = v_loc[0]; i <= v_loc[1]; ++i){
                                                if(integers)vmem_int_subset[c++] = vmem->mmap[i];
                                                else vmem_str_subset[c++] = vmem->cp_mmap[i];
                                          }
                                    }
                                    // this will run for a long time so we might as well free up whatever memory we can
                                    free_mem_map(vmem, integers);
                                    BYTE to_w_b[int_mode_bytes];
                                    if(integers)memcpy(to_w_b, &to_w_i, int_mode_bytes);
                                    while(1){ // child process will forever repeat this
                                          // sleeping to limit cpu usage
                                          usleep(1000);
                                          for(int i = 0; i <= v_loc[1]-v_loc[0]; ++i){
                                                if(integers){
                                                      if(same){
                                                            to_w_i = vmem_int_subset[i].value;
                                                            memcpy(to_w_b, &to_w_i, int_mode_bytes);
                                                      }
                                                      write_bytes_to_pid_mem(vmem->pid, int_mode_bytes, vmem_int_subset[i].addr, to_w_b);
                                                }
                                                else{
                                                      if(same)strcpy(to_w, vmem_str_subset[i].value);
                                                      // TODO: make it possible to write strings containing \0
                                                      write_str_to_pid_mem(vmem->pid, vmem_str_subset[i].addr, to_w);
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
                                    child_pid[num_locks-1].m_addr = vmem->mmap[v_loc[0]].addr;
                                    // if we're locking values using "_" notation don't try atoi
                                    if(strcmp(to_w, "_") != 0){
                                          child_pid[num_locks-1].i_value = atoi(to_w);
                                    }
                                    else child_pid[num_locks-1].i_value = vmem->mmap[v_loc[0]].value;
                              }
                              else child_pid[num_locks-1].m_addr = vmem->cp_mmap[v_loc[0]].addr;
                              puts("variable(s) locked");
                              update_mem_map(vmem, integers);
                              //goto Find; // TODO: decide what behavior should be after vars have been locked
                              continue;
                        }
                        BYTE to_w_b[int_mode_bytes];
                        int tmp_i;
                        for(int i = v_loc[0]; i <= v_loc[1]; ++i){
                              if(integers){
                                    tmp_i = atoi(to_w);
                                    memcpy(to_w_b, &tmp_i, int_mode_bytes);
                                    write_bytes_to_pid_mem(vmem->pid, int_mode_bytes, vmem->mmap[i].addr, to_w_b);
                              }
                              else{
                                    if(to_w_len > strlen(vmem->cp_mmap[i].value)){
                                          printf("WARNING (%i: %p): writing a string that is larger than the original string in its memory location causes undefined behavior\n", vmem->pid, vmem->cp_mmap[i].addr);
                                          free(vmem->cp_mmap[i].value);
                                          // allocating enough space for updated string in mmap and 
                                          // hoping that writing it doesn't infringe on other strings
                                          vmem->cp_mmap[i].value = malloc(sizeof(char)*to_w_len+1);
                                          // this string can contain anything as long as its length == strlen(to_w)
                                          // it's about to be overwritten by update_mem_map
                                          memset(vmem->cp_mmap[i].value, '1', to_w_len);

                                    }
                                    // TODO: add option to zero entire string
                                    write_str_to_pid_mem(vmem->pid, vmem->cp_mmap[i].addr, to_w);
                                    if(to_w[to_w_len-2] == '\\' && to_w[to_w_len-1] == '0'){
                                          // write terminated string if \0 found
                                          write_bytes_to_pid_mem(vmem->pid, 1, (void*)(((char*)vmem->cp_mmap[i].addr)+to_w_len-2), (BYTE*)"");
                                    }
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
            if((strcmp(tmp_str, "") == 0 || !valid_int(tmp_str)) && integers){
                  puts("enter a valid integer to search");
                  goto Find;
            }
            else if(strcmp(tmp_str, "") == 0){
                  puts("enter a valid string to search");
                  goto Find;
            }
            if(first)populate_mem_map(vmem, vmem->pid, d_rgn, additional, integers, int_mode_bytes);
            if(strcmp(tmp_str, "\\w") == 0 || strcmp(tmp_str, "\\u") == 0 || strcmp(tmp_str, "\\q") == 0 || strcmp(tmp_str, "\\r") == 0 || strcmp(tmp_str, "\\?") == 0){
                  // allow searching for 'w' or 'u' with \w or \u
                  tmp_str[0] = tmp_str[1]; tmp_str[1] = '\0';
            }
            if(strcmp(tmp_str, "\\rv") == 0 || strcmp(tmp_str, "\\rl") == 0){
                  tmp_str[0] = tmp_str[1]; tmp_str[1] = tmp_str[2]; tmp_str[2] = '\0';
            }
            if(!first)update_mem_map(vmem, integers);
            if(integers){
                  tmp_val = atoi(tmp_str);
                  narrow_mem_map_int(vmem, tmp_val);
            }
            else narrow_mem_map_str(vmem, tmp_str, false);
            if(vmem->size == 0){
                  printf("nothing matches your search of: %s\nresetting mem map\n", tmp_str);
                  // setting first to true to imitate behavior of first search and load, reducing space complexity by waiting to repopulate mem_map
                  first = true;
                  goto Find;
            }
            else{
                  if(!verbose && vmem->size > result_print_limit){
                        printf("your search of %s has %li results\nresult_print_limit is set at %li. refusing to print\n", tmp_str, vmem->size, result_print_limit);
                  }
                  else{
                        puts("matches are now:");
                        print_mmap(vmem, "", integers, print_rgns);
                  }
            }
            first = false;
      }
}

int main(int argc, char* argv[]){
      char help_str[] = "NOTE: this program will not work without root privileges\n<pid> {[-p [filter]] [-r <virtual memory address>] [-w <virtual memory address> <value>] [-i] [-f] [-sb <filename>] [-wb <filename>] [-S] [-H] [-B] [-A] [-E] [-C] [-b <integer>] [-v] [-pr] [-pl <print limit>]}\n    -p  : prints all variables in specified memory region with corresponding virtual memory addresses. optional filter\n    -r  : read single value from virtual memory address\n    -w  : write single value to virtual memory address\n    -i  : inverts all 1s and 0s in specified memory region\n    -f  : interactive mode (default)\n    -sb : save backup of process memory to file (not yet implemented)\n    -wb : restore process memory to backup (not yet implemented)\n    -S  : use stack (default)\n    -H  : use heap\n    -B  : use both heap and stack\n    -A  : look for additional memory regions\n    -E  : use all available memory regions\n    -C  : use char/string mode\n    -b  : set number of bytes to read at a time in integer mode\n    -v  : verbose mode (enables print region mode)\n    -pr : print region that memory addresses are found in\n    -pl : set print limit for search results (only affects interactive mode, can be useful for small screens)";

      if(argc == 1 || (argc > 1 && strcmp(argv[1], "-h") == 0)){
            puts(help_str);
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
      // TODO: fix criteria for unmarked additional mem rgns in vmem_parser.cpp
      vmem.mapped_rgn = get_vmem_locations(pid, false); // disabling unmarked additional rgns until criteria for unmarked additional mem rgns are fixed
      if(!mem_rgn_warn(d_rgn, vmem.mapped_rgn, additional))return -1;
      if(argc > 2){
            // -r and -w can be used without slowly loading a complete mem_map
            if(strcmp(argv[2], "-r") == 0){
                  if(integers)printf("%i\n", read_single_val_from_pid_mem(pid, n_bytes, (void*)strtoul(argv[3], 0, 16)));
                  // read_str_from_mem_block_slow must be used because string size is unknown
                  else printf("%s\n", read_str_from_mem_block_slow(pid, (void*)strtoul(argv[3], 0, 16), NULL));
                  return 0;
            }
            if(strcmp(argv[2], "-w") == 0){
                  if(integers){
                        int tmp_i = atoi(argv[4]);
                        BYTE to_w[n_bytes];
                        memcpy(to_w, &tmp_i, n_bytes);
                        write_bytes_to_pid_mem(pid, n_bytes, (void*)strtoul(argv[3], 0, 16), to_w);
                  }
                  else write_str_to_pid_mem(pid, (void*)strtoul(argv[3], 0, 16), argv[4]);
                  return 0;
            }
            if(strcmp(argv[2], "-f") == 0){
                  SAFE_INTER:
                  vmem.pid = pid;
                  if(interactive_mode(&vmem, integers, n_bytes, d_rgn, additional, verbose, result_print_limit, print_rgns)){
                        free_mem_map(&vmem, integers);
                  }
                  // we need to free mapped_rgn even if mem_map wasn't populated
                  free_mem_rgn(&vmem.mapped_rgn);
                  return 0;
            }
            if(strcmp(argv[2], "-p") == 0){
                  populate_mem_map(&vmem, pid, d_rgn, additional, integers, n_bytes);
                  if(argc > 3 && argv[3][0] != '-'){
                        print_mmap(&vmem, argv[3], integers, print_rgns);
                  }
                  else print_mmap(&vmem, "", integers, print_rgns);
                  free_mem_map(&vmem, integers);
                  free_mem_rgn(&vmem.mapped_rgn);
                  return 0;
            }
            if(strcmp(argv[2], "-i") == 0){
                  if(!integers){
                        puts("cannot invert string/char*");
                        free_mem_map(&vmem, false);
                        free_mem_rgn(&vmem.mapped_rgn);
                        return -1;
                  }
                  populate_mem_map(&vmem, pid, d_rgn, additional, integers, 1);
                  logic_swap(&vmem);
                  free_mem_map(&vmem, integers);
                  free_mem_rgn(&vmem.mapped_rgn);
                  return 0;
            }
            goto SAFE_INTER;
      }
      // argc <= 2
      // default to interactive mode
      goto SAFE_INTER;
}
