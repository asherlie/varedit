#include <string.h>
#include "ashio.h"

#ifdef shared
#include <vmem_access.h>
#else
#include "vmem_access.h"
#endif

bool strtoi(const char* str, unsigned int* ui, int* i){
      char* res;
      unsigned int r = (unsigned int)strtol(str, &res, 10);
      if(*res)return false;
      if(i)*i = (int)r;
      if(ui)*ui = r;
      return true;
}

bool strtop(const char* str, void** p){
      char* res;
      if(p)*p = (void*)strtoul(str, &res, 16);
      else strtoul(str, &res, 16);
      return !*res;
}

bool mem_rgn_warn(int d_rgn, struct mem_rgn mem, bool additional, bool silent){
      bool no_ad = (mem.n_remaining == 0 && additional);
      bool stack = true;
      if(no_ad){
            if(!silent)fputs("WARNING: no valid unmarked memory regions were found\n", stderr);
            if(d_rgn == NONE)return false;
      }
      if((d_rgn == STACK || d_rgn == BOTH) && (mem.stack.start == NULL || mem.stack.end == NULL)){
            if(!silent)fputs("WARNING: no valid stack memory region was found\n", stderr);
            if(d_rgn == STACK && (no_ad || !additional))return false;
            stack = false;
      }
      if((d_rgn == HEAP || d_rgn == BOTH) && (mem.heap.start == NULL || mem.heap.end == NULL)){
            if(!silent)fputs("WARNING: no valid heap memory region was found\n", stderr);
            if((d_rgn == HEAP || (d_rgn == BOTH && !stack)) && (no_ad || !additional))return false;
      }
      return true;
}

void print_ih_mmap(const struct mem_map* mem, _Bool show_rgns){
      for(int i = 0; i < mem->i_mmap_hash.n_bux; ++i)
            for(int j = 0; j < mem->i_mmap_hash.bucket_ref[i]; ++j){
                  if(show_rgns)printf("%p (%s) : %i\n", mem->i_mmap_hash.i_buckets[i][j].addr, which_rgn(mem->mapped_rgn,
                                                                     mem->i_mmap_hash.i_buckets[i][j].addr, NULL),
                                      *mem->i_mmap_hash.i_buckets[i][j].value);
                  else printf("%p: %i\n", mem->i_mmap_hash.i_buckets[i][j].addr, *mem->i_mmap_hash.i_buckets[i][j].value);

            }
}

void print_mmap(const struct mem_map* mem, bool show_rgns){
      if(mem->integers && mem->i_mmap_hash.in_place){
            print_ih_mmap(mem, show_rgns);
            return;
      }
      for(unsigned int i = 0; i < mem->size; ++i){
            if(mem->integers){
                  if(show_rgns)printf("%p (%s) : %i\n", mem->i_mmap[i].addr, which_rgn(mem->mapped_rgn, mem->i_mmap[i].addr, NULL), *mem->i_mmap[i].value);
                  else printf("%p: %i\n", mem->i_mmap[i].addr, *mem->i_mmap[i].value);
            }
            else{
                  if(show_rgns)printf("%p (%s) : \"%s\"\n", mem->s_mmap[i].addr, which_rgn(mem->mapped_rgn, mem->s_mmap[i].addr, NULL), mem->s_mmap[i].value);
                  else printf("%p: \"%s\"\n", mem->s_mmap[i].addr, mem->s_mmap[i].value);
            }
      }
}

/* ch_p is an abstraction of the deprecated null_char_parse and caret_parse */
bool ch_p(char* chr, char* str, bool beg){
      char* c = str;
      unsigned int chrlen = strlen(chr);
      while((c = strstr(c, chr))){
            if(c > str && *(c-1) == '\\'){
                  for(char* i = c-1; *i != '\0'; ++i)*i = *(i+1);
                  c += chrlen;
                  continue;
            }
            if(beg){
                  unsigned int cl = strlen(c+1);
                  // memmove because of overlapping pointers
                  memmove(str, c+1, cl);
                  str[cl] = '\0';
                  return true;
            }
            else{
                  *c = '\0';
                  return true;
            }
      }
      return false;
}

/* this was added to fix a bug that occured when strings are cut short with a \0 and the literal '\' and '0' were 
 * also written, which was apparent when the null byte was later overwritten, exposing the rest of the string */
bool null_char_parse(char* str){
      return ch_p("\\0", str, false);
}

// used to parse the ^ char that demarcates beginning of string
bool caret_parse(char* str){
      return ch_p("^", str, true);
}

struct narrow_pth_arg{
      _Bool _int, * first, int_pop;
      struct mem_map** mem;
      char* s_match;
      int d_rgn, additional,
      
      /* used only for populating int mmaps */
      int_bytes,

      sterm_cap,

      /* chars_read refers to total chars read between all calls */
      chars_read, 

      /* input_{sz,cap} keep track of all search history */
      input_sz, input_cap;

      /* used for iterative renarrowing */
      char** sterms, ** all_input;

      struct gr_subroutine_arg* gsa;
};

bool first_cmd(char* str){
      return *str == 'w' || *str == 'q' || *str == '?' || *str == 'u' || *str == 'r';
}

void pis(struct narrow_pth_arg* npa){
      for(int i = 0; i< npa->input_sz; ++i)
            printf("%i: \"%s\"\n", i, npa->all_input[i]);
}

void* narrow_pth(void* npa_v){
      struct narrow_pth_arg* npa = (struct narrow_pth_arg*)npa_v;
      /* if int mode, we'll still take advantage of this time to populate */
      if(*npa->first){
            populate_mem_map(*npa->mem, npa->d_rgn, npa->additional, npa->_int, npa->int_bytes);
            npa->int_pop = 1;
            *npa->first = 0;
      }
      if(npa->_int)return NULL;
      /* TODO: commands should be prepended by '/' */
      /* if this could be a command, don't even bother */
      if(first_cmd(*npa->gsa->str_recvd))return NULL;
      /* TODO: what if del is read as the first character -- test this */
      _Bool del = *npa->gsa->char_recvd == 8 || *npa->gsa->char_recvd == 127;

      npa->chars_read += (del) ? -1 : 1;

      /*if(!npa->chars_read)return NULL;*/
      #if 0
      investigate this
      if(0 && !npa->chars_read){
            *npa->first = 1;
            free_mem_map(*npa->mem);
            return NULL;
      }
      #endif

      /* since we're using the very expensive
       * re-narrowing method, we'll need to completely
       * reset our mem map each time we read a del
       */
      /* re-narrow */
      if(del){
            /*puts("\n\n\ndel read\n\n");*/
            free(npa->sterms[npa->chars_read]);
            /*puts("\rusing follwing to revert");*/
            /*pis(npa);*/
            /*free_mem_map(*npa->mem);*/
            populate_mem_map(*npa->mem, npa->d_rgn, npa->additional, 0, -1);
            /* probably not necessary */
            *npa->first = 0;

            /* narrow all srings saved in all_input */
            /* before this iteration we should adjust for consecutive
             * searches that are redundant
             * if a search is a subset of a previous one
             * delete it
             */
            char* tmp_str_ptr;
            for(int i = 0; i < npa->input_sz; ++i){
                  tmp_str_ptr = npa->all_input[i];
                  narrow_mem_map_str(*npa->mem, tmp_str_ptr, caret_parse(tmp_str_ptr), ch_p("$", tmp_str_ptr, false));
                  if((*npa->mem)->size == 0){
                        /**npa->first = true;*/
                        return NULL;
                  }
            }
      }

      if(!npa->chars_read)return NULL;

      if(!del){
            if(npa->chars_read == npa->sterm_cap){
                  npa->sterm_cap *= 2;
                  char** tmp_sterm = malloc(sizeof(char*)*npa->sterm_cap);
                  memcpy(tmp_sterm, npa->sterms, sizeof(char*)*npa->chars_read);
                  free(npa->sterms);
                  npa->sterms = tmp_sterm;
            }
            npa->sterms[npa->chars_read-1] = strdup(*npa->gsa->str_recvd);
      }

      char* tmp_str_ptr;
      /*tmp_str_ptr = npa->sterms[npa->chars_read-1];*/
      tmp_str_ptr = *npa->gsa->str_recvd;
      narrow_mem_map_str(*npa->mem, tmp_str_ptr, caret_parse(tmp_str_ptr), ch_p("$", tmp_str_ptr, false));

      return NULL;
}

void free_sterms(struct narrow_pth_arg* npa, _Bool base, _Bool preserve_last){
      for(int i = 0; i < npa->chars_read-preserve_last; ++i)
            free(npa->sterms[i]);
      if(base){
            free(npa->sterms);
            for(int i = 0; i < npa->input_sz; ++i)
                  free(npa->all_input[i]);
            free(npa->all_input);
      }
}

/* each time this is called, we'll back up the current list of sterms */
void reset_sterms(struct narrow_pth_arg* npa){
      free_sterms(npa, 0, 1);
      if(!npa->chars_read)return;
      if(npa->input_sz == npa->input_cap){
            npa->input_cap *= 2;
            char** tmp_inp = malloc(sizeof(char*)*npa->input_cap);
            memcpy(tmp_inp, npa->all_input, sizeof(char*)*npa->input_sz);
            free(npa->all_input);
            npa->all_input = tmp_inp;
      }
      npa->all_input[npa->input_sz++] = npa->sterms[npa->chars_read-1];
      npa->chars_read = 0;
}

bool interactive_mode(struct mem_map* vmem, bool integers, int int_mode_bytes, int d_rgn, int additional, bool verbose, unsigned int result_print_limit, bool print_rgns){
      _Bool no_sub = 
      #ifdef NO_SUB
      1
      #else
      0
      #endif
      ;

      char search_mode_help[620];
      char* prog = search_mode_help;
      if(!integers)prog = stpcpy(search_mode_help, "NOTE: '^' marks the beginning of a target string of our search, it will only accept exact matches to the start of a string\nNOTE: '$' marks the end of a target string of our search, it will only accept exact matches to the end of a string\nsearch mode options:\n    wa <value> : write single value to all current results\n    <string> : enter a string to narrow results - begin string with \"^\" to match beginning exactly,\n               end string with \"$\" to match end exactly or use delimeter '\\' to search for '?', 'q', 'u', 'r', 'w'\n    ");
      else prog = stpcpy(prog, "    <integer> : enter an integer to narrow results\n    ");
      strcpy(prog, "r : reset mem map\n    u : update visible values\n    ? : show this\n    q : quit");
      char write_mode_help[564];
      prog = write_mode_help;
      if(!integers)prog = stpncpy(prog, "NOTE: a \"\\0\" in any write string will be replaced with a NUL character unless escaped with a \'\\\'\n", 124);
      strncpy(prog, "NOTE: <memory location reference #> can be replaced with <start reference #>-<end reference #>\nwrite mode options:\n    <memory location reference #> <value to write> : writes value to memory location(s)\n    l <memory location reference #> <value to write> : locks memory location(s) to provided value\n    l <memory location reference #> _ : locks memory location(s) to their current value(s)\n    rl <lock number> : remove specified lock\n    ? : show this\n    q : quit", 468);
      printf("in interactive mode on process %i (%s)\nusing ", vmem->mapped_rgn.pid, vmem->mapped_rgn.p_name);
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
      char* tmp_str = NULL;
      char* to_w = NULL;
      int tmp_strlen = 0;
      int tmp_val, grs_ignore[] = {9, 0};
      /* first_search is equivalent to first but it does not get
       * altered by any other functions/threads
       *
       * first refers to population, first_search to searches
       * TODO: is first actually necessary?
       */
      bool first = true, first_search = true, lock_mode, tab;
      struct lock_container lock_pids;
      size_t to_w_sz = 0;
      lock_container_init(&lock_pids, 1);

      struct gr_subroutine_arg gsa;
      struct narrow_pth_arg npa;

      npa.first = &first;
      npa.gsa = &gsa;
      npa.mem = &vmem;
      npa._int = integers;
      npa.d_rgn = d_rgn;
      npa.additional = additional;

      npa.chars_read = 0;
      npa.sterm_cap = 20;
      npa.sterms = malloc(sizeof(char*)*npa.sterm_cap);

      npa.input_cap = 100;
      npa.input_sz = 0;
      npa.all_input = malloc(sizeof(char*)*npa.input_cap);

      npa.int_bytes = int_mode_bytes;


      init_gsa(&gsa);
      gsa.pthread_arg = &npa;

      // TODO: possibly get rid of while loop in favor of goto Find for increased clarity
      while(1){
            /* NOTE: each goto Find could be replaced by a continue to return to this point and the label Find could be removed
             * gotos are used intentionally to make code clearer */
            Find:
            fputs("enter current variable value to search", stdout);
            if(!first_search)fputs(" or 'w' to enter write mode", stdout);
            fputs("\n", stdout);
            npa.int_pop = 0;
            tmp_str = (no_sub || vmem->low_mem) ? getline_raw(&tmp_strlen, &tab, grs_ignore) : getline_raw_sub(&tmp_strlen, &tab, grs_ignore, narrow_pth, &gsa);
            puts("");
            if(!no_sub && !vmem->low_mem)pthread_join(gsa.prev_th, NULL);
            /*
             * at this point we can reset sterms
             * because they're only useful between rapid calls to narrow_pth()
            */
            reset_sterms(&npa);
            /*tmp_str[tmp_strlen] = '\0';*/
            if(strncmp(tmp_str, "q", 2) == 0){
                  free_locks(&lock_pids, 3);
                  if(to_w)free(to_w);
                  if(!no_sub && !vmem->low_mem)free_gsa(&gsa);
                  free(tmp_str);
                  free_sterms(&npa, 1, 0);
                  return !first;
            }
            // TODO: add ability to rescan memory regions and update vmem->mapped_rgn
            if(strncmp(tmp_str, "?", 2) == 0){
                  puts(search_mode_help);
                  goto Find;
            }
            if(strncmp(tmp_str, "u", 2) == 0){
                  if(!first_search){
                        update_mem_map(vmem);
                        fseek(stdin, 0, SEEK_END);
                        print_mmap(vmem, print_rgns);
                  }
                  goto Find;
            }
            if(strncmp(tmp_str, "r", 2) == 0){
                  if(vmem->size != 0){
                        free_mem_map(vmem);
                        vmem->size = 0;
                        first_search = first = true;
                        /* TODO: free strings */
                        npa.input_sz = 0;
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
                        // this was producing an incorrect uninitialized variable warning
                        #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
                        bool nul;
                        if(integers){
                              if(!strtoi(tmp_str+3, NULL, &tmp_val)){
                                    printf("\"%s\" is not a valid integer\n", tmp_str+3);
                                    goto Find;
                              }
                              memcpy(write, &tmp_val, int_mode_bytes);
                        }
                        else nul = null_char_parse(tmp_str+3);
                        for(unsigned int i = 0; i < vmem->size; ++i){
                              if(integers)write_bytes_to_pid_mem(vmem->mapped_rgn.pid, int_mode_bytes, vmem->i_mmap[i].addr, write);
                              else{
                                    // if our write string is larger than destination string, resize destination string
                                    if(tmp_strlen-3 > (int)strlen(vmem->s_mmap[i].value)){
                                          if(vmem->blk->in_place){
                                                memset(vmem->s_mmap[i].value, 1, tmp_strlen-3);
                                                for(char* p = vmem->s_mmap[i].value+tmp_strlen-3; *p <= 0 || *p >= 127; ++p)*p = 1;
                                          }
                                          else{
                                                free(vmem->s_mmap[i].value);
                                                vmem->s_mmap[i].value = malloc(sizeof(char)*tmp_strlen-3+5+1);
                                                memset(vmem->s_mmap[i].value, 1, tmp_strlen+5+1);
                                          }
                                    }
                                    // writing adjusted tmp_str with corrected length
                                    write_str_to_pid_mem(vmem->mapped_rgn.pid, vmem->s_mmap[i].addr, tmp_str+3);
                                    if(nul)write_bytes_to_pid_mem(vmem->mapped_rgn.pid, 1, (void*)(((char*)vmem->s_mmap[i].addr)+strlen(tmp_str+3)), (BYTE*)"");
                              }
                        }
                        if(integers)printf("wrote %s to %i memory locations\n", tmp_str+3, vmem->size);
                        else printf("wrote \"%s\" to %i memory locations\n", tmp_str+3, vmem->size);
                        goto Find;
                  }
            }
            if(strncmp(tmp_str, "w", 2) == 0){
                  if(first_search){
                        puts("no memory locations available for writing. returning to search");
                        goto Find;
                  }
                  char v_loc_s[10];
                  // v_loc stores start and end of range
                  unsigned int v_loc[2];
                  unsigned short to_w_len = 0;
                  while(1){
                        Write:
                        // TODO: possibly update_mem_map here to print accurate values in write mode
                        // update_mem_map(vmem);
                        if(integers)
                              for(unsigned int i = 0; i < vmem->size; ++i)
                                    printf("%i: (%p: %i)\n", i, vmem->i_mmap[i].addr, *vmem->i_mmap[i].value);
                        else
                              for(unsigned int i = 0; i < vmem->size; ++i)
                                    printf("%i: (%p: \"%s\")\n", i, vmem->s_mmap[i].addr, vmem->s_mmap[i].value);
                        printf("enter a number from [0-%i] or a range with a '-' followed by a value to write OR 's' to continue searching\n", vmem->size-1);
                        // width is 1 less than length of length of v_loc_s to avoid overwriting '\0'
                        // ignore leading whitespace
                        scanf(" %9[^ \t\n]%*c", v_loc_s);
                        if(strncmp(v_loc_s, "s", 2) == 0){
                              fseek(stdin, 0, SEEK_END);
                              print_mmap(vmem, print_rgns);
                              goto Find;
                        }
                        if(strncmp(v_loc_s, "q", 2) == 0){
                              free_locks(&lock_pids, 3);
                              if(to_w)free(to_w);
                              if(!no_sub && !vmem->low_mem)free_gsa(&gsa);
                              free(tmp_str);
                              free_sterms(&npa, 1, 0);
                              return !first;
                        }
                        if(strncmp(v_loc_s, "?", 2) == 0){
                              puts(write_mode_help);
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        if(strncmp(v_loc_s, "u", 2) == 0){
                              update_mem_map(vmem);
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        if(strncmp(v_loc_s, "pl", 3) == 0){
                              if(!print_locks(&lock_pids))puts("no locks are currently in place");
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        if(strncmp(v_loc_s, "rl", 3) == 0){
                              // if no locks are in place
                              if(!print_locks(&lock_pids)){
                                    puts("no locks are currently in place");
                                    // ignoring the rest of the input stream in case a number has been entered after "rl"
                                    char c;
                                    while((c = getchar()) != '\n' && c != EOF);
                              }
                              else{
                                    fgets(v_loc_s, 10, stdin);
                                    if(v_loc_s[strlen(v_loc_s)-1] == '\n')v_loc_s[strlen(v_loc_s)-1] = '\0';
                                    // lock_pids.n + 1 will always be out of bounds - this is to make sure that no lock will be removed after invalid input
                                    unsigned int rm_s = lock_pids.n + 1;
                                    if(!strtoi(v_loc_s, &rm_s, NULL) || rm_s >= lock_pids.n-lock_pids.n_removed)
                                          puts("enter a valid integer");
                                    long i = remove_lock(&lock_pids, rm_s, true, 1);
                                    // remove_lock returns -1 if rm_s is out of bounds
                                    if(i >= 0){
                                          if(integers){
                                                printf("lock with value %i removed\n", *lock_pids.locks[i].i_val);
                                                free(lock_pids.locks[i].i_val);
                                          }
                                          else{
                                                printf("lock with value \"%s\" removed\n", *lock_pids.locks[i].s_val);
                                                // *s_val must be freed because we used keep_first so we could print *s_val
                                                free(*lock_pids.locks[i].s_val);
                                                free(lock_pids.locks[i].s_val);
                                          }
                                    }
                              }
                              fseek(stdin, 0, SEEK_END);
                              goto Write;
                        }
                        lock_mode = false;
                        if(strncmp(v_loc_s, "l", 2) == 0){
                              lock_mode = true;
                              scanf(" %9[^ \t\n]%*c", v_loc_s);
                        }
                        to_w_len = getline(&to_w, &to_w_sz, stdin)-1;
                        to_w[to_w_len] = '\0';
                        int to_w_i;
                        if(integers && !strtoi(to_w, NULL, &to_w_i) && !(lock_mode && strncmp(to_w, "_", 2) == 0)){
                              puts("enter a valid integer to write");
                              goto Write;
                        }
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
                              unsigned int n_addr = v_loc[1]-v_loc[0]+1;
                              // TODO: free addrs
                              void** addrs = malloc(sizeof(void*)*n_addr); unsigned int addr_s = 0;
                              char** chars; int* ints;
                              if(integers)ints = malloc(sizeof(int)*n_addr);
                              else chars = malloc(sizeof(char*)*n_addr);
                              bool same = strncmp(to_w, "_", 2) == 0;
                              bool mul_val = false;
                              if(n_addr > 1)mul_val = same;
                              if(!same){
                                    if(integers)*ints = to_w_i;
                                    else *chars = strdup(to_w);
                              }
                              for(unsigned int i =  v_loc[0]; i <= v_loc[1]; ++i){
                                    if(integers){
                                          addrs[addr_s++] = vmem->i_mmap[i].addr;
                                          if(same)ints[i] = *vmem->i_mmap[i].value;
                                    }
                                    else{
                                          addrs[addr_s++] = vmem->s_mmap[i].addr;
                                          if(same)chars[i] = strdup(vmem->s_mmap[i].value);
                                    }
                              }
                              create_lock(&lock_pids, vmem->mapped_rgn.pid, addrs, ints, chars, n_addr, mul_val, integers);
                              puts((n_addr > 1) ? "variables locked" : "variable locked");
                              update_mem_map(vmem);
                              continue;
                        }
                        BYTE to_w_b[int_mode_bytes];
                        bool nul;
                        if(!integers)nul = null_char_parse(to_w);
                        for(unsigned int i = v_loc[0]; i <= v_loc[1]; ++i){
                              if(integers){
                                    memcpy(to_w_b, &to_w_i, int_mode_bytes);
                                    write_bytes_to_pid_mem(vmem->mapped_rgn.pid, int_mode_bytes, vmem->i_mmap[i].addr, to_w_b);
                              }
                              else{
                                    if(to_w_len > strlen(vmem->s_mmap[i].value)){
                                          fprintf(stderr, "WARNING (%i: %p): writing a string that is larger than the original string in its memory location causes undefined behavior\n", vmem->mapped_rgn.pid, vmem->s_mmap[i].addr);
                                          // TODO: confirm that writing of MUCH larger strings than destination is supported
                                          if(vmem->blk->in_place){
                                               /* TODO: add bounds checking to make sure we're not writing past BYTE* - blk + rgn.end-rgn.start
                                                * TODO: possibly overallocate each block str by enough to allow for reasonable string extensions
                                                * possibly ignore this, as it's very unlikely to occur
                                                * filling bytes immediately after to_w[to_w_len] to make room for a longer string
                                                * this comes in handy when overwriting a null byte at the beginning of a string
                                                * we can get away with possibly not having enough space before the next string
                                                * by calling the writing of a larger string ~~undefined behavior~~
                                                */
                                                memset(vmem->s_mmap[i].value, 1, to_w_len);
                                                for(char* p = vmem->s_mmap[i].value+to_w_len; *p <= 0 || *p >= 127; ++p)*p = 1;
                                          }
                                          else{
                                               /* allocating enough space for updated string in mmap
                                                * this string can contain anything as long as its length >= strlen(to_w)
                                                * it's about to be overwritten by update_mem_map anyway, which will
                                                * ensure our individually malloc'd string has enough space for to_w
                                                */
                                                free(vmem->s_mmap[i].value);
                                                vmem->s_mmap[i].value = malloc(sizeof(char)*to_w_len+5+1);
                                                memset(vmem->s_mmap[i].value, 1, to_w_len+5+1);
                                          }
                                    }
                                    write_str_to_pid_mem(vmem->mapped_rgn.pid, vmem->s_mmap[i].addr, to_w);
                                    // write terminated string if \0 found
                                    if(nul)write_bytes_to_pid_mem(vmem->mapped_rgn.pid, 1, (void*)(((char*)vmem->s_mmap[i].addr)+strlen(to_w)), (BYTE*)"");
                              }
                        }
                        update_mem_map(vmem); // to make sure accurate values are printed
                  }
            }
            // tmp_str != "w"
            // checking if input is valid before populating mem_map
            if(!*tmp_str || (integers && !strtoi(tmp_str, NULL, NULL))){
                  if(integers)puts("enter a valid integer to search");
                  else puts("enter a valid string to search");
                  goto Find;
            }
            if(((integers && !npa.int_pop) || vmem->low_mem || no_sub) && first)populate_mem_map(vmem, d_rgn, additional, integers, int_mode_bytes);
            // tmp_str_ptr makes it easier to handle escaped searches of reserved varedit strings because it can be incremented
            char* tmp_str_ptr = tmp_str;
            // to deal with escaped \w, \u, \q, \r, \?, \rl
            if(tmp_str[0] == '\\')++tmp_str_ptr;
            
            if(!first_search)update_mem_map(vmem);
            if(integers){
                  tmp_val = atoi(tmp_str_ptr);
                  narrow_mem_map_int(vmem, tmp_val);
            }
            // if caret_parse evaluates to true, exact_s, if ch_p $, exact_e
            // if search string contains escaped ^ as well as escaped $, only one set will be found because escape chars are stripped with each ch_p call
            // TODO: add param to ch_p - bool rm_escape_char

            /* if we're not in low mem mode and this isn't our first iteration,
             * it will be safe to assume that getline_raw_sub() has already narrowed 
             * our mem_map
             */
            else if(no_sub || vmem->low_mem || first || first_cmd(tmp_str))narrow_mem_map_str(vmem, tmp_str_ptr, caret_parse(tmp_str_ptr), ch_p("$", tmp_str_ptr, false));
            if(vmem->size == 0){
                  printf("nothing matches your search of: %s\nresetting mem map\n", tmp_str_ptr);
                  // setting first to true to imitate behavior of first search and load, reducing space complexity by waiting to repopulate mem_map
                  first_search = first = true;
                  goto Find;
            }
            if(!verbose && vmem->size > result_print_limit)
                  printf("your search of %s has %i results\nresult_print_limit is set at %i. refusing to print\n", tmp_str_ptr, vmem->size, result_print_limit);
            else{
                  puts("matches are now:");
                  print_mmap(vmem, print_rgns);
            }
            first = false;
            first_search = false;
            free(tmp_str);
      }
}

int main(int argc, char* argv[]){
      char ver[] = "varedit 1.5.0";
      char help_str[1023] = " <pid> {[-p [filter]] [-r <memory address>] [-w <memory address> <value>] [-i] [-S] [-H] [-B] [-A] [-E] [-U] [-C] [-b <n bytes>] [-V] [-pr] [-pl <print limit>]}\n"
      "    -p  : prints values in specified memory region with optional filter\n"
      "    -r  : read single value from virtual memory address\n"
      "    -w  : write single value to virtual memory address\n"
      "    -i  : interactive mode (default)\n"
      "    -S  : use stack\n"
      "    -H  : use heap\n"
      "    -B  : use both heap and stack\n"
      "    -A  : look for additional memory regions\n"
      "    -E  : use all available memory regions (default)\n"
      "    -U  : use unmarked additional regions (very slow)\n"
      "    -C  : use char/string mode\n"
      "    -b  : set number of bytes to read at a time in integer mode (must be <= sizeof(int))\n"
      "    -V  : verbose (enables print region and ignores result_print_limit)\n"
      "    -pr : print region that memory addresses are found in\n"
      "    -pl : set print limit for search results (only affects interactive mode, can be useful for small screens)";
      strncpy(help_str+966, "\x66\x6f\x72\x20\x6d\x65\x65\x6e\x61\x20\x61\x6e\x64\x20\x68\x61\x73\x6b\x65\x6c\x6c\x2c\x20\x6d\x79\x20\x73\x65\x63\x6f\x6e\x64\x20\x61\x6e\x64\x20\x66\x69\x72\x73\x74\x20\x6c\x6f\x76\x65\x73\x0", 50);
      if(argc == 1 || (argc > 1 && strncmp(argv[1], "-h", 3) == 0)){
            printf("usage: %s", argv[0]);
            puts(help_str);
            return -1;
      }
      bool integers=true, additional=false, verbose=false, print_rgns=false, unmarked=false;
      int d_rgn = NONE, n_bytes=4, result_print_limit=100;
      // stores argv index of previous value setting argument
      int p = -1;
      // default to interactive mode
      char mode = 'i';
      // stores argument indices of argv for p, r, w modes
      int args[2];
      pid_t pid;
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
                              case 'b': if(!(argc > i+1) || !strtoi(argv[i+1], NULL, &n_bytes) || n_bytes == 0 || n_bytes > 4)n_bytes = 4; else if(p != -2)p = i+1; break;
                              case 'V': verbose = true; print_rgns = true; break;
                              case 'v':{
                                          _Bool no_sub =
                                          #ifdef NO_SUB
                                          1
                                          #else
                                          0
                                          #endif
                                          ;
                                          printf("%s using %s and ashio %s\nlow mem mode is %s, subroutine re-narrowing is %s\n",
                                          ver, MEMCARVE_VER, ASHIO_VER, (LOW_MEM) ?  "enabled" : "disabled", (!no_sub) ? "enabled" : "disabled"); return -1;
                                       }
                              // TODO: -p will sometimes be used without a filter str
                              case 'p': mode = 'p'; if(p != -2)p = i+1; args[0] = i+1; break; 
                              case 'r': mode = 'r'; if(p != -2)p = i+1; args[0] = i+1; break;
                              case 'w': mode = 'w'; if(p != -2)p = i+2; args[0] = i+1; args[1] = i+2; break;
                              case 'i': mode = 'i'; break;
                        }
                  }
                  // strlen == 3 and begins with -p
                  else if(argv[i][1] == 'p' && argv[i][2] && !argv[i][3]){
                        switch(argv[i][2]){
                              case 'r': print_rgns = true; break;
                              case 'l': if(!(argc > i+1) || !strtoi(argv[i+1], NULL, &result_print_limit))result_print_limit = 100; else if(p != -2)p = i+1; break;
                        }
                  }
            }
            // p == -2 when pid has been set, >= i when used as an argument for a flag
            else if(p != -2 && p < i && strtoi(argv[i], NULL, &pid))p = -2;
      }
      if(p != -2){
            puts("enter a valid pid");
            return -1;
      }
      // default to stack, heap and additional if no region is specified
      if(d_rgn == NONE && !additional){
            d_rgn = BOTH;
            additional = true;
      }
      struct mem_map vmem;
      mem_map_init(&vmem, pid, unmarked);
      // TODO: fix criteria for unmarked additional mem rgns in vmem_parser.c, too many regions are being recorded
      // no warnings are printed unless we're in interactive mode
      if(!mem_rgn_warn(d_rgn, vmem.mapped_rgn, additional, verbose || mode != 'i')){
            puts("no usable memory regions found\nyou DO have root privileges, don't you");
            free_mem_rgn(&vmem.mapped_rgn);
            return -1;
      }
      // TODO: possibly translate this to a switch statement
      // 0 is run, 1 invalid address, 2, invalid integer to write, 3, both
      unsigned char not_run = 0;
      if(mode == 'r'){
            void* addr;
            if(argc <= args[0] || !strtop(argv[args[0]], &addr))not_run = 1;
            else{
                  if(integers)printf("%i\n", read_single_val_from_pid_mem(pid, n_bytes, addr));
                  // read_str_from_mem_range_slow must be used because string size is unknown
                  else{
                        char* str = read_str_from_mem_range_slow(pid, addr, NULL);
                        puts(str);
                        free(str);
                  }
            }
      }
      else if(mode == 'w'){
            void* addr;
            if(argc <= args[0] || !strtop(argv[args[0]], &addr))not_run = 1;
            if(integers){
                  int tmp_i;
                  if(argc <= args[1] || !strtoi(argv[args[1]], NULL, &tmp_i))not_run += 2;
                  else if(!not_run){
                        BYTE to_w[n_bytes];
                        memcpy(to_w, &tmp_i, n_bytes);
                        write_bytes_to_pid_mem(pid, n_bytes, addr, to_w);
                  }
            }
            else if(!not_run)write_str_to_pid_mem(pid, addr, argv[args[1]]);
      }
      else if(mode == 'i'){
            if(interactive_mode(&vmem, integers, n_bytes, d_rgn, additional, verbose, result_print_limit, print_rgns))
                  free_mem_map(&vmem);
      }
      else if(mode == 'p'){
            populate_mem_map(&vmem, d_rgn, additional, integers, n_bytes);
            // search strings beginning with '-' must be escaped with '\'
            // TODO: document this
            // if we're going to try to narrow
            if(argc > args[0] && *argv[args[0]] != '-'){
                  char* filt = argv[args[0]];
                  filt += *filt == '\\';
                  if(!integers)narrow_mem_map_str(&vmem, filt, caret_parse(filt), ch_p("$", filt, false));
                  else if(strtoi(filt, NULL, &args[1]))narrow_mem_map_int(&vmem, args[1]);
            }
            print_mmap(&vmem, print_rgns);
            if(vmem.size != 0)free_mem_map(&vmem);
      }
      if(not_run == 1 || not_run == 3)puts("enter a valid address");
      if(not_run == 2 || not_run == 3)puts("enter a valid integer to write");
      free_mem_rgn(&vmem.mapped_rgn);
      // not_run will be non-zero when an error has been found
      return not_run;
}
