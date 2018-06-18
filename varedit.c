#include <string.h>
#include <signal.h> // kill()
#include <sys/wait.h> // wait()

#include "vmem_access.h"

bool strtoi(const char* str, int* i){
      char* res;
      if(i)*i = (int)strtol(str, &res, 10);
      else strtol(str, &res, 10);
      return !*res;
}

bool mem_rgn_warn(int d_rgn, struct mem_rgn mem, bool additional){
      bool no_ad = (mem.n_remaining == 0 && additional);
      bool stack = true;
      if(no_ad){
            fputs("WARNING: no valid unmarked memory regions were found\n", stderr);
            if(d_rgn == NONE)return false;
      }
      if((d_rgn == STACK || d_rgn == BOTH) && (mem.stack.start == NULL || mem.stack.end == NULL)){
            fputs("WARNING: no valid stack memory region was found\n", stderr);
            if(d_rgn == STACK && (no_ad || !additional))return false;
            stack = false;
      }
      if((d_rgn == HEAP || d_rgn == BOTH) && (mem.heap.start == NULL || mem.heap.end == NULL)){
            fputs("WARNING: no valid heap memory region was found\n", stderr);
            if((d_rgn == HEAP || (d_rgn == BOTH && !stack)) && (no_ad || !additional))return false;
      }
      return true;
}

int remove_volatile_values(struct mem_map* vmem){
      int n = 0;
      for(unsigned int i = 0; i < vmem->size; ++i){
            for(int in = 0; in < 10; ++in){
                  if(vmem->mmap[i].value != read_single_val_from_pid_mem(vmem->pid, 4, vmem->mmap[i].addr)){
                        vmem->mmap[i--] = vmem->mmap[--vmem->size];
                        ++n;
                  }
            }
      }
      return n;
}

// TODO: possibly merge this with update_mem_map and give update_mem_map a param bool print_mmap
void print_mmap(const struct mem_map* mem, const char* contains, bool integers, bool show_rgns){
      bool cont = strlen(contains) != 0;
      int i_cont = 0;
      if(integers && cont)strtoi(contains, &i_cont);
      for(unsigned int i = 0; i < mem->size; ++i){
            if(integers){
                  if(!cont || mem->mmap[i].value == i_cont){
                        if(show_rgns)printf("%p (%s) : %i\n", mem->mmap[i].addr, which_rgn(mem->mapped_rgn, mem->mmap[i].addr, NULL), mem->mmap[i].value);
                        else printf("%p: %i\n", mem->mmap[i].addr, mem->mmap[i].value);
                  }
            }
            else{
                  if(!cont || strstr(mem->cp_mmap[i].value, contains)){
                        if(show_rgns)printf("%p (%s) : \"%s\"\n", mem->cp_mmap[i].addr, which_rgn(mem->mapped_rgn, mem->cp_mmap[i].addr, NULL), mem->cp_mmap[i].value);
                        else printf("%p: \"%s\"\n", mem->cp_mmap[i].addr, mem->cp_mmap[i].value);
                  }
            }
      }
}

/* this was added to fix a bug that occured when strings are cut short with a \0 and the literal '\' and '0' were 
 * also written, which was apparent when the null byte was later overwritten, exposing the rest of the string */
bool null_char_parse(char* str){
      char* null_char = str;
      while((null_char = strstr(null_char, "\\0"))){
            if(null_char > str && *(null_char-1) == '\\'){
                  unsigned int j;
                  --null_char;
                  for(j = 0; j < strlen(null_char)-1; ++j)null_char[j] = null_char[j+1];
                  null_char[j] = '\0';
                  null_char += 3;
                  continue;
            }
            memset(null_char, '\0', strlen(null_char)-1);
            return true;
      }
      return false;
}

void print_locks(struct lock_container* locks, unsigned char num_locks, unsigned char l_removed, bool integers){
      if(num_locks-l_removed == 0){
            puts("no locks are currently in place");
            return;
      }
      int r_i = 0;
      for(unsigned char i = 0; i < num_locks; ++i){
            if(locks[i].m_addr == NULL)continue;
            if(integers)printf("(%i) %p: %i", r_i, locks[i].m_addr, locks[i].i_value);
            else printf("(%i) %p: \"%s\"", r_i, locks[i].m_addr, locks[i].s_value);
            if(locks[i].rng)puts(" (multiple locks)"); else puts("");
            ++r_i;
      }
}

bool interactive_mode(struct mem_map* vmem, bool integers, int int_mode_bytes, int d_rgn, int additional, bool verbose, unsigned int result_print_limit, bool print_rgns){
      char search_mode_help[650];
      char* prog;
      prog = stpcpy(search_mode_help, "search mode options:\n    r : reset mem map\n    wa <value> : write single value to all current results\n    ");
      if(integers)prog = stpcpy(prog, "<integer> : enter an integer to narrow results\n    rv : remove volatile variables\n    ");
      else prog = stpcpy(prog, "<string> : enter a string to narrow results - end string with \"\\0\" to search for exact strings or use delimeter '\\' to search for '?', 'q', 'u', 'r', 'w'\n    ");
      strcpy(prog, "u : update visible values\n    ? : show this\n    q : quit");
      char write_mode_help[] = "NOTE: <memory location reference #> can be replaced with <start reference #>-<end reference #>\nwrite mode options:\n    <memory location reference #> <value to write> : writes value to memory location(s)\n    l <memory location reference #> <value to write> : locks memory location(s) to provided value\n    l <memory location reference #> _ : locks memory location(s) to their current value(s)\n    rl <lock number> : remove specified lock\n    ? : show this\n    q : quit";
      printf("in interactive mode on process %i (%s)\nusing ", vmem->pid, vmem->mapped_rgn.p_name);
      if(d_rgn == STACK)printf("stack");
      if(d_rgn == HEAP)printf("heap");
      if(d_rgn == BOTH)printf("both stack and heap");
      if(additional && vmem->mapped_rgn.n_remaining != 0){
            if(d_rgn != NONE)printf(" as well as %i additional memory regions", vmem->mapped_rgn.n_remaining);
            else printf("%i additional memory regions", vmem->mapped_rgn.n_remaining);
      }
      printf(" - looking for ");
      if(integers)puts("integers");
      else puts("strings");
      puts("enter 'u' at any time to update visible values, 'q' to exit or '?' for help");
      // tmp_str needs to be large enough for any search string
      // TODO: read in chunks/use getline to assure large enough string/not to overuse memory
      char tmp_str[4096];
      unsigned short tmp_strlen = 0;
      int tmp_val;
      bool first = true;
      bool lock_mode;
      // the five variables below are used to keep track of var locks
      unsigned char lock_cap = 1;
      unsigned char num_locks = 0;
      unsigned char l_removed = 0;
      struct lock_container* lock_pids = malloc(sizeof(struct lock_container)*lock_cap);
      pid_t temp_pid;
      while(1){
            Find:
            printf("enter current variable value to search");
            if(!first)printf(" or 'w' to enter write mode");
            puts("");
            fgets(tmp_str, 4096, stdin);
            tmp_strlen = strlen(tmp_str);
            tmp_str[tmp_strlen-1]='\0';
            if(strcmp(tmp_str, "q") == 0){
                  for(unsigned char i = 0; i < num_locks; ++i){
                        if(lock_pids[i].m_addr == NULL)continue;
                        kill(lock_pids[i].pid, SIGKILL);
                        wait(NULL);
                  }
                  free(lock_pids);
                  return !first;
            }
            // TODO: add ability to rescan memory regions and update vmem->mapped_rgn
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
            // wa mode
            if(tmp_strlen > 3){
                  if(tmp_str[0] == 'w' && tmp_str[1] == 'a' && tmp_str[2] == ' '){
                        // for int mode
                        BYTE write[int_mode_bytes];
                        bool nul;
                        if(integers){
                              if(!strtoi(tmp_str+3, &tmp_val)){
                                    printf("\"%s\" is not a valid integer\n", tmp_str+3);
                                    goto Find;
                              }
                              memcpy(write, &tmp_val, int_mode_bytes);
                        }
                        else nul = null_char_parse(tmp_str+3);
                        for(unsigned int i = 0; i < vmem->size; ++i){
                              if(integers)write_bytes_to_pid_mem(vmem->pid, int_mode_bytes, vmem->mmap[i].addr, write);
                              else{
                                    // if our write string is larger than destination string, resize destination string
                                    if(tmp_strlen-3 > (int)strlen(vmem->cp_mmap[i].value)){
                                          if(vmem->blk->in_place){
                                                memset(vmem->cp_mmap[i].value, 1, tmp_strlen-3);
                                                for(char* p = vmem->cp_mmap[i].value+tmp_strlen-3; *p <= 0 || *p >= 127; ++p)*p = 1;
                                          }
                                          else{
                                                free(vmem->cp_mmap[i].value);
                                                vmem->cp_mmap[i].value = malloc(sizeof(char)*tmp_strlen-3+5+1);
                                                memset(vmem->cp_mmap[i].value, 1, tmp_strlen+5+1);
                                          }
                                    }
                                    // writing adjusted tmp_str with corrected length
                                    write_str_to_pid_mem(vmem->pid, vmem->cp_mmap[i].addr, tmp_str+3);
                                    if(nul)write_bytes_to_pid_mem(vmem->pid, 1, (void*)(((char*)vmem->cp_mmap[i].addr)+strlen(tmp_str+3)), (BYTE*)"");
                              }
                        }
                        if(integers)printf("wrote %s to %i memory locations\n", tmp_str+3, vmem->size);
                        else printf("wrote \"%s\" to %i memory locations\n", tmp_str+3, vmem->size);
                        goto Find;
                  }
            }
            if(strcmp(tmp_str, "w") == 0){
                  // to_w needs to be large enough to store any write string
                  // TODO: make to_w char* and use getline()
                  char v_loc_s[10], to_w[4096];
                  unsigned int v_loc[2]; // v_loc stores start and end of range
                  unsigned short to_w_len = 0;
                  while(1){
                        Write:
                        if(first){
                              puts("no memory locations available for writing. returning to search");
                              goto Find;
                        }
                        // TODO: possibly update_mem_map here to print accurate values in write mode
                        // update_mem_map(vmem, integers);
                        if(integers)
                              for(unsigned int i = 0; i < vmem->size; ++i){
                                    printf("%i: (%p: %i)\n", i, vmem->mmap[i].addr, vmem->mmap[i].value);
                        }
                        else{
                              for(unsigned int i = 0; i < vmem->size; ++i)
                                    printf("%i: (%p: \"%s\")\n", i, vmem->cp_mmap[i].addr, vmem->cp_mmap[i].value);
                        }
                        printf("enter a number from [0-%i] or a range with a '-' followed by a value to write OR 's' to continue searching\n", vmem->size-1);
                        // width is 1 less than length of length of v_loc_s to avoid overwriting '\0'
                        // ignore leading whitespace
                        scanf(" %9[^ \t\n]%*c", v_loc_s);
                        if(strcmp(v_loc_s, "s") == 0){
                              fseek(stdin, 0, SEEK_END);
                              print_mmap(vmem, "", integers, print_rgns);
                              goto Find;
                        }
                        if(strcmp(v_loc_s, "q") == 0){
                              for(unsigned char i = 0; i < num_locks; ++i){
                                    if(lock_pids[i].m_addr == NULL)continue;
                                    kill(lock_pids[i].pid, SIGKILL);
                                    wait(NULL);
                              }
                              free(lock_pids);
                              return !first;
                        }
                        if(strcmp(v_loc_s, "?") == 0){
                              puts(write_mode_help);
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        if(strcmp(v_loc_s, "u") == 0){
                              update_mem_map(vmem, integers);
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        if(strcmp(v_loc_s, "pl") == 0){
                              print_locks(lock_pids, num_locks, l_removed, integers);
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        if(strcmp(v_loc_s, "rl") == 0){
                              if(num_locks-l_removed == 0)puts("no locks are currently in place");
                              else{
                                    print_locks(lock_pids, num_locks, l_removed, integers);
                                    // TODO: abstract lock removal to a function so it can be easily used in find mode
                                    // TODO: possibly add built in lock functionality to vmem_access with updated lock struct that contains l_removed, num_locks, etc.
                                    fgets(v_loc_s, 10, stdin);
                                    if(v_loc_s[strlen(v_loc_s)-1] == '\n')v_loc_s[strlen(v_loc_s)-1] = '\0';
                                    int rm_s;
                                    if(!strtoi(v_loc_s, &rm_s) || rm_s >= num_locks-l_removed || rm_s < 0){
                                          puts("enter a valid integer");
                                          goto Write;
                                    }
                                    int r_i = 0;
                                    for(int i = 0; i < num_locks; ++i){
                                          if(lock_pids[i].m_addr == NULL)continue;
                                          if(r_i == rm_s){
                                                kill(lock_pids[i].pid, SIGKILL);
                                                wait(NULL);
                                                ++l_removed;
                                                if(integers)printf("lock with value %i removed (%p)\n", lock_pids[i].i_value, lock_pids[i].m_addr);
                                                else printf("lock with value \"%s\" removed (%p)\n", lock_pids[i].s_value, lock_pids[i].m_addr);
                                                // setting to null as to not print it
                                                lock_pids[i].m_addr = NULL;
                                          }
                                          ++r_i;
                                    }
                              }
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        lock_mode = false;
                        if(strcmp(v_loc_s, "l") == 0){
                              lock_mode = true;
                              scanf(" %9[^ \t\n]%*c", v_loc_s);
                        }
                        scanf("%4095[^\t\n]%*c", to_w);
                        int to_w_i;
                        if(integers && !strtoi(to_w, &to_w_i) && !(lock_mode && strcmp(to_w, "_") == 0)){
                              puts("enter a valid integer to write");
                              goto Write;
                        }
                        to_w_len = strlen(to_w);
                       { // scope to limit e_r's lifetime
                              char* e_r = strchr(v_loc_s, '-');
                              if(e_r != NULL)*(e_r++) = '\0';
                             { // to limit scope of res, tmp_s
                              // setting both indices in case not range
                              // using strtoul instead of strtoi to avoid truncating unsigned ints
                              char* res;
                              unsigned int tmp_s = strtoul(v_loc_s, &res, 10);
                              if(!*res)v_loc[1] = v_loc[0] = tmp_s;
                              else{
                                    Int_err:
                                    puts("enter a valid integer or range of integers");
                                    goto Write;
                              }
                              if(e_r != NULL)tmp_s = strtoul(e_r, &res, 10);
                              if(!*res)v_loc[1] = tmp_s;
                              else goto Int_err;
                             }
                              if(v_loc[0] >= vmem->size || v_loc[1] >= vmem->size)goto Int_err;
                       }
                        if(lock_mode){
                              temp_pid = fork();
                              if(temp_pid == 0){
                                    bool same = false;
                                    if(strcmp(to_w, "_") == 0)same = true;
                                    // creating pair arrays to store relevant addresses and values so i can free up memory
                                    struct addr_int_pair vmem_int_subset [v_loc[1]-v_loc[0]+1];
                                    struct addr_str_pair vmem_str_subset[v_loc[1]-v_loc[0]+1];
                                   { // creating a scope to limit c's lifetime
                                          int c = 0;
                                          for(unsigned int i = v_loc[0]; i <= v_loc[1]; ++i){
                                                // setting vmem subsets regardless of same
                                                // if !same, these are just used for addr
                                                if(integers)vmem_int_subset[c++] = vmem->mmap[i];
                                                else{
                                                      vmem_str_subset[c] = vmem->cp_mmap[i];
                                                      // need to copy string to new char* to avoid it being freed by free_mem_map
                                                      vmem_str_subset[c].value = strdup(vmem_str_subset[c].value);
                                                      ++c;
                                                }
                                          }
                                   }
                                    // this will run for a long time so we might as well free up whatever memory we can
                                    free_mem_map(vmem, integers);
                                    BYTE to_w_b[int_mode_bytes];
                                    if(integers)memcpy(to_w_b, &to_w_i, int_mode_bytes);
                                    char* w = to_w;
                                    bool nul = null_char_parse(w);
                                    while(1){ // child process will forever repeat this
                                          // sleeping to limit cpu usage
                                          usleep(1000);
                                          for(unsigned int i = 0; i <= v_loc[1]-v_loc[0]; ++i){
                                                if(integers){
                                                      if(same){
                                                            to_w_i = vmem_int_subset[i].value;
                                                            memcpy(to_w_b, &to_w_i, int_mode_bytes);
                                                      }
                                                      write_bytes_to_pid_mem(vmem->pid, int_mode_bytes, vmem_int_subset[i].addr, to_w_b);
                                                      if(nul)write_bytes_to_pid_mem(vmem->pid, 1, (void*)(((char*)vmem->cp_mmap[i].addr)+strlen(to_w)), (BYTE*)"");
                                                }
                                                else{
                                                      if(same)w = vmem_str_subset[i].value;
                                                      // TODO: make it possible to lock strings containing \0
                                                      write_str_to_pid_mem(vmem->pid, vmem_str_subset[i].addr, w);
                                                }
                                          }
                                    }
                              }
                              // writing raw string to lock_pids regardless of string/int mode - this avoids the need to handle strings separately from ints
                              if(num_locks == lock_cap){
                                    lock_cap *= 2;
                                    struct lock_container* tmp_lock = malloc(sizeof(struct lock_container)*lock_cap);
                                    memcpy(tmp_lock, lock_pids, sizeof(struct lock_container)*num_locks);
                                    free(lock_pids);
                                    lock_pids = tmp_lock;
                              }
                              ++num_locks;
                              // TODO: possibly remove locks already in vmem->mmap[v_loc[0]].addr
                              // TODO: possibly make lock_pids->m_addr a *, with every mem loc in it
                              // although it might add complexity for no reason
                              lock_pids[num_locks-1].s_value = to_w;
                              lock_pids[num_locks-1].pid = temp_pid;
                              lock_pids[num_locks-1].rng = !(v_loc[0] == v_loc[1]);
                              if(integers){
                                    lock_pids[num_locks-1].m_addr = vmem->mmap[v_loc[0]].addr;
                                    // if we're locking values using "_" notation don't try to convert to_w to int
                                    if(strcmp(to_w, "_") != 0)lock_pids[num_locks-1].i_value = to_w_i;
                                    else lock_pids[num_locks-1].i_value = vmem->mmap[v_loc[0]].value;
                              }
                              else lock_pids[num_locks-1].m_addr = vmem->cp_mmap[v_loc[0]].addr;
                              puts("variable(s) locked");
                              update_mem_map(vmem, integers);
                              continue;
                        }
                        BYTE to_w_b[int_mode_bytes];
                        bool nul;
                        if(!integers)nul = null_char_parse(to_w);
                        for(unsigned int i = v_loc[0]; i <= v_loc[1]; ++i){
                              if(integers){
                                    memcpy(to_w_b, &to_w_i, int_mode_bytes);
                                    write_bytes_to_pid_mem(vmem->pid, int_mode_bytes, vmem->mmap[i].addr, to_w_b);
                              }
                              else{
                                    if(to_w_len > strlen(vmem->cp_mmap[i].value)){
                                          fprintf(stderr, "WARNING (%i: %p): writing a string that is larger than the original string in its memory location causes undefined behavior\n", vmem->pid, vmem->cp_mmap[i].addr);
                                          if(vmem->blk->in_place){
                                               /* TODO: add bounds checking to make sure we're not writing past BYTE*
                                                * filling bytes immediately after to_w[to_w_len] to make room for a longer string
                                                * this comes in handy when overwriting a null byte at the beginning of a string
                                                */
                                                memset(vmem->cp_mmap[i].value, 1, to_w_len);
                                                for(char* p = vmem->cp_mmap[i].value+to_w_len; *p <= 0 || *p >= 127; ++p)*p = 1;
                                          }
                                          else{
                                               /* allocating enough space for updated string in mmap
                                                * this string can contain anything as long as its length >= strlen(to_w)
                                                * it's about to be overwritten by update_mem_map anyway, which will
                                                * ensure our individually malloc'd string has enough space for to_w
                                                */
                                                free(vmem->cp_mmap[i].value);
                                                vmem->cp_mmap[i].value = malloc(sizeof(char)*to_w_len+5+1);
                                                memset(vmem->cp_mmap[i].value, 1, to_w_len+5+1);
                                          }
                                    }
                                    // TODO: possibly add option to zero entire string
                                    write_str_to_pid_mem(vmem->pid, vmem->cp_mmap[i].addr, to_w);
                                    // write terminated string if \0 found
                                    if(nul)write_bytes_to_pid_mem(vmem->pid, 1, (void*)(((char*)vmem->cp_mmap[i].addr)+strlen(to_w)), (BYTE*)"");
                              }
                        }
                        update_mem_map(vmem, integers); // to make sure accurate values are printed
                  }
            }
            // tmp_str != "w"
            if(strcmp(tmp_str, "rv") == 0){ // rv for remove volatile - disqualifies all volatile vars
                  if(integers){
                        printf("%i volatile variables removed\n", remove_volatile_values(vmem));
                        goto Find;
                  }
            }
            // checking if input is valid before calling populating mem_map
            if(!*tmp_str || (integers && !strtoi(tmp_str, NULL))){
                  if(integers)puts("enter a valid integer to search");
                  else puts("enter a valid string to search");
                  goto Find;
            }
            if(first)populate_mem_map(vmem, vmem->pid, d_rgn, additional, integers, int_mode_bytes);
            // tmp_str_ptr makes it easier to handle escaped searches of reserved varedit strings because it can be incremented
            char* tmp_str_ptr = tmp_str;
            // to deal with escaped \w, \u, \q, \r, \?, \rv, \rl
            if(tmp_str[0] == '\\')++tmp_str_ptr;
            if(!first)update_mem_map(vmem, integers);
            if(integers){
                  tmp_val = atoi(tmp_str_ptr);
                  narrow_mem_map_int(vmem, tmp_val);
            }
            // if null_char_parse evaluates to true, we will want to do an exact string narrow
            else narrow_mem_map_str(vmem, tmp_str_ptr, null_char_parse(tmp_str_ptr));
            if(vmem->size == 0){
                  printf("nothing matches your search of: %s\nresetting mem map\n", tmp_str_ptr);
                  // setting first to true to imitate behavior of first search and load, reducing space complexity by waiting to repopulate mem_map
                  first = true;
                  goto Find;
            }
            if(!verbose && vmem->size > result_print_limit)
                  printf("your search of %s has %i results\nresult_print_limit is set at %i. refusing to print\n", tmp_str_ptr, vmem->size, result_print_limit);
            else{
                  puts("matches are now:");
                  print_mmap(vmem, "", integers, print_rgns);
            }
            first = false;
      }
}

int main(int argc, char* argv[]){
      char help_str[960] = " <pid> {[-p [filter]] [-r <memory address>] [-w <memory address> <value>] [-i] [-S] [-H] [-B] [-A] [-E] [-U] [-C] [-b <n bytes>] [-v] [-pr] [-pl <print limit>]}\n"
      "    -p  : prints values in specified memory region with optional filter\n"
      "    -r  : read single value from virtual memory address\n"
      "    -w  : write single value to virtual memory address\n"
      "    -i  : interactive mode (default)\n"
      "    -S  : use stack (default)\n"
      "    -H  : use heap\n"
      "    -B  : use both heap and stack\n"
      "    -A  : look for additional memory regions\n"
      "    -E  : use all available memory regions\n"
      "    -U  : use unmarked additional regions (very slow)\n"
      "    -C  : use char/string mode\n"
      "    -b  : set number of bytes to read at a time in integer mode\n"
      "    -v  : verbose (enables print region and ignores result_print_limit)\n"
      "    -pr : print region that memory addresses are found in\n"
      "    -pl : set print limit for search results (only affects interactive mode, can be useful for small screens)";
      printf("help motha A%li\n", strlen(help_str));
      strncpy(help_str+941, "\x66\x6f\x72\x20\x6d\x65\x65\x6e\x61", 10);
      if(argc == 1 || (argc > 1 && strcmp(argv[1], "-h") == 0)){
            printf("usage: %s", argv[0]);
            puts(help_str);
            return -1;
      }
      bool integers=true, additional=false, verbose=false, print_rgns=false, unmarked=false;
      int d_rgn = NONE, n_bytes=4, result_print_limit=100;
      for(int i = 1; i < argc; ++i){
            if(*argv[i] == '-'){
                  // if strlen == 2
                  if(argv[i][1] && !argv[i][2]){
                        switch(argv[i][1]){
                              case 'S': d_rgn = STACK; break;
                              case 'H': d_rgn = HEAP; break;
                              case 'B': d_rgn = BOTH; break;
                              case 'A': additional = true; break;
                              case 'E': additional = true; d_rgn = BOTH; break;
                              case 'U': unmarked = true; break;
                              case 'C': integers = false; break;
                              case 'b': if(!(argc > i+1) || !strtoi(argv[i+1], &n_bytes))n_bytes = 4; break;
                              case 'v': verbose = true; print_rgns = true; break;
                        }
                  }
                  // strlen == 3 and begins with -p
                  else if(argv[i][1] == 'p' && argv[i][2] && !argv[i][3]){
                        switch(argv[i][2]){
                              case 'r': print_rgns = true; break;
                              case 'l': if(!(argc > i+1) || !strtoi(argv[i+1], &result_print_limit))result_print_limit = 100; break;
                        }
                  }
            }
      }
      // default to stack if no region specified
      if(d_rgn == NONE && !additional)d_rgn = STACK;
      pid_t pid;
      if(!strtoi(argv[1], &pid)){
            puts("enter a valid pid");
            return -1;
      }
      // initializing vmem here extends scope to default behavior to avoid rescanning memory
      struct mem_map vmem;
      // TODO: fix criteria for unmarked additional mem rgns in vmem_parser.c, too many regions are being recorded
      vmem.mapped_rgn = get_vmem_locations(pid, unmarked);
      if(!mem_rgn_warn(d_rgn, vmem.mapped_rgn, additional)){
            puts("you DO have root privileges, don't you");
            return -1;
      }
      if(argc > 2){
            // -r and -w can be used without slowly loading a complete mem_map
            if(strcmp(argv[2], "-r") == 0){
                  if(integers)printf("%i\n", read_single_val_from_pid_mem(pid, n_bytes, (void*)strtoul(argv[3], NULL, 16)));
                  // read_str_from_mem_range_slow must be used because string size is unknown
                  else{
                        char* str = read_str_from_mem_range_slow(pid, (void*)strtoul(argv[3], NULL, 16), NULL);
                        printf("%s\n", str);
                        free(str);
                  }
                  free_mem_rgn(&vmem.mapped_rgn);
                  return 0;
            }
            if(strcmp(argv[2], "-w") == 0){
                  if(integers){
                        int tmp_i;
                        if(!strtoi(argv[4], &tmp_i))puts("enter a valid integer");
                        else{
                              BYTE to_w[n_bytes];
                              memcpy(to_w, &tmp_i, n_bytes);
                              write_bytes_to_pid_mem(pid, n_bytes, (void*)strtoul(argv[3], NULL, 16), to_w);
                        }
                  }
                  else write_str_to_pid_mem(pid, (void*)strtoul(argv[3], NULL, 16), argv[4]);
                  free_mem_rgn(&vmem.mapped_rgn);
                  return 0;
            }
            if(strcmp(argv[2], "-i") == 0){
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
                  // TODO: allow escaped '-' in search string
                  if(argc > 3 && argv[3][0] != '-'){
                        print_mmap(&vmem, argv[3], integers, print_rgns);
                  }
                  else print_mmap(&vmem, "", integers, print_rgns);
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
