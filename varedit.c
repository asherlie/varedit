#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "ashio.h"

#ifdef shared
#include <vmem_access.h>
#else
#include "vmem_access.h"
#endif

// TODO: fix weird double frees. this program is so broken.
// maybe rewrite from ground up to be fast, lock free, functional
// i can probably use vmem_access but rewrite varedit
//
// TODO: locks should be running in one thread - granularity can be updated for them
//       just have a struct that stores many locks with address and value. one loop can set all of them
//       periodically

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

/*
 * i should maybe just store all bytes in one huge array. yes keep them in a bytearray - there shouldn't be an int/char* mode
 * makes so much more sense to just read memory in sizeof(x) chunks, although we don't know at what offset which type of variable lives
 * 
 * okay, the way to do this is to do n_threads passes over the large chunk of memory each initial scan.
 * we don't know where variables begin, so we have to iterate with a sliding scale
*/

void find_var(pid_t pid) {
    // can't forget this
    struct mem_map_optimized m;
    char* ln = NULL;
    size_t sz;
    ssize_t ln_len;
    int val = 54;
    init_mem_map_opt(&m);
    add_frame(&m, "test frame");
    m.rgn = get_vmem_locations(pid, 1);

    while (1) {
        ln_len = getline(&ln, &sz, stdin);
        ln[ln_len-1] = 0;
        val = atoi(ln);
        /*hmm, for some reason we're failing to repopulate with strings only*/
        if (!populate_mem_map_opt(&m, 1, 1, 1)) {
            puts("failed to populate, exiting.");
            break;
        }
        /*narrow_mem_map_frame_opt(&m, &m.frames[0], 1, &val, 4, &heap, &stack, &other);*/
        narrow_mem_map_frame_opt(&m, &m.frames[0], 1, ln, strlen(ln) - 1);
        /*printf("%i %i %i - %i total matches for %i!\n", heap, stack, other, m.frames[0].n_tracked, val);*/
        printf("%i total matches for \"%s\"!\n", m.frames[0].n_tracked, ln);
        (void)val;
        if (m.frames[0].n_tracked <= 33) {
            /*p_frame_var(&m, &m.frames[0]);*/
            p_frame_var(&m, (&m.frames[0]), "s", char*);
            p_frame_var(&m, (&m.frames[0]), "f", float);
        }
    }
}

/*
 * we'll:
 *  getline()
 *
 *  mode agnostic:
     *  update - just prints all vars in current frame, doesn't narrow
     *  frame operations?
     *      create frame? create frame with name
     *      remove frame? rm frame
     *      swap frame? switch to idx or label

 *  search mode?
     *  check type 
     *  begins and ends with " ? string
     *  has a decimal point? float
     *  ends with a D? double
     *  ends with an L? long
     *  isnum()? int
 *
 * edit mode?
 *      lock? lock
 *      wa - write all
 *      w <idx> val
 * edit mode?
 */
enum i_mode { SEARCH, EDIT };

/* returns whether a string was found so we know whether to print a string */
// TODO: support doubles, longs
void* str_to_val(char* str, ssize_t len, uint16_t* valsz, enum type_found* found) {
    _Bool dec = 0, d_found = 0, l_found = 0;
    char* endptr;
    void* ret = NULL;

    if (*str == '"' && str[len-1] == '"') {
        str[len-1] = 0;
        *valsz = len;
        *found = STRING;
        // TODO: make a copy, this is really bad for multiple reasons
        return str + 1;
    }
    if (isalpha(str[len-1])) {
        if (str[len-1] == 'D') {
           d_found = 1; 
        } else if (str[len-1] == 'L') {
           l_found = 1; 
        }
        str[len-1] = 0;
    }
    for (ssize_t i = 0; i < len; ++i) {
        /* exit before handling the \0 we inserted above */
        if (!str[i]) {
            break;
        }
        if (str[i] == '.') {
            dec = 1;
            puts("found dec");
            break;
        }
    }

    if (d_found) {
        *valsz = sizeof(double);
        double d = strtod(str, &endptr);
        *found = DOUBLE;
        ret = malloc(*valsz);
        memcpy(ret, &d, *valsz);
        if (endptr == str) {
            puts("FAILED TO CONV");
        }
    } else if (dec){
        // handle regular floats
        *valsz = sizeof(float);
        ret = malloc(*valsz);
        float f = strtof(str, &endptr);
        *found = FLOAT;
        memcpy(ret, &f, *valsz);
    } else {
        *valsz = sizeof(int);
        if (l_found) {
            *valsz = sizeof(long);
            *found = LONG;
        } else {
            *found = INT;
        }
        ret = malloc(*valsz);
        long li = strtol(str, &endptr, 10);
        memcpy(ret, &li, *valsz);
    }

    return ret;
}

// this is bad, we shouldn't be doing this over and over while iterating. this needs to be two functions still
// convert, write. this way we can keep all these args out of here too - just have this take in
// m, v, vsz
//
// have caller get val from raw string.
void write_var(struct mem_map_optimized* m, void* val, int valsz, struct found_variable* v) {
/*
 *     uint16_t valsz;
 *     void* val;
 * 
 *     printf("converting %s\n", raw_str);
 *     val = str_to_val(raw_str, strlen, &valsz, t);
*/
    write_bytes_to_pid_mem(m->rgn.pid, valsz, (void*)get_remote_addr(m, v), (BYTE*)val);
}

void frame_operation(struct mem_map_optimized* m, struct narrow_frame** current_frame, char* arg, int arglen) {
    void* write_val;
    uint16_t valsz;
    switch(arg[2]) {
        // /f frame create
        case 'c':
            add_frame(m, arg + 4);
            break;
        // /fs frame select
        case 's':
            break;
        // /fw frame write
        case 'w':
            write_val = str_to_val(arg + 4, arglen - 4, &valsz, &(*current_frame)->current_type);
            for (struct found_variable* v = (*current_frame)->tracked_vars; v; v = v->next) {
                write_var(m, write_val, valsz, v);
            }
            break;
        default:
        // /fl frame list
        case 'l':
            printf("%i frames\n", m->n_frames);
            for (struct narrow_frame* f = m->frames; f; f = f->next) {
                printf("\"%s\" - tracking %i %s variables\n", f->label, f->n_tracked, type_to_str(f->current_type));
            }
            break;
    }
}

_Bool interactive_mode_opt(struct mem_map_optimized* m) {
    char* ln = NULL;
    ssize_t ln_len;
    enum i_mode mode = SEARCH;
    // TODO: ensure that thread number doesn't change found number of variables
    // uhh, it directly impacts it LOL, seemingly with values that don't even match
    // then after a couple narrows they're all the same. look into this.
    // this problem even exists with 0 threads, but gets worse with each added thread
    uint8_t n_threads = 10;
    struct narrow_frame* frame = m->frames;

    void* val;
    uint16_t valsz;

    clock_t c_st, c_end;

    // enable readline history
    using_history();

    while (1) {
        /*ln_len = getline(&ln, &sz, stdin);*/
        ln = readline("ve ~> ");
        ln_len = strlen(ln);

        add_history(ln);
        /*
         * if (ln[ln_len-1] == '\n') {
         *     ln[--ln_len] = 0;
         * }
        */
        
        // handle frame operations and mode switches here
        // TODO: need to be able to create and switch between frames
        // this will let us print out frame labels and switch between them!
        // each frame will have a diff thing i can manipulate like money, missiles, etc.!
        // TODO: add a feature to set all values in a frame to a certain value - like:
        //  /frameset health 30
        //  /frameset money 99999
        if (*ln == '/') {
            switch(ln[1]) {
                case 'q':
                    return 1;
                // reset
                case 'r':
                    /*frame->*/
                    break;
                case 'f':
                    printf("current frame: \"%s\"\n", frame->label);
                    frame_operation(m, &frame, ln, ln_len);
                    break;
                case 's':
                    mode = SEARCH;
                    break;
                case 'e':
                    mode = EDIT;
                    break;
                case 'u':
                    populate_mem_map_opt(m, 1, 1, 1);
                    // fall through
                // this is a temp case to show how we'll print
                case 'l':
                    /*
                     * change f str to enum fir type! print floats and longs doubles etc. ALSO
                     * store most recent search type to mark frame type in frame ops!
                     * recent will change with writes too,. maybe change it from here directly!@
                    */
                    if (frame->current_type == STRING) {
                        p_frame_var(m, frame, "s", char*);
                    } else if (frame->current_type == INT){
                        p_frame_var(m, frame, "i", int); 
                    } else if (frame->current_type == FLOAT){
                        p_frame_var(m, frame, "f", float); 
                    } else if (frame->current_type == DOUBLE){
                        p_frame_var(m, frame, "lf", double); 
                    } else if (frame->current_type == LONG){
                        p_frame_var(m, frame, "li", long); 
                    }
                    break;
                case 'w':
                    val = str_to_val(ln + 3, ln_len - 3, &valsz, &frame->current_type);
                    for (struct found_variable* v = frame->tracked_vars; v; v = v->next) {
                        /*write_bytes_to_pid_mem(m->rgn.pid, valsz, (void*)get_remote_addr(m, v), (BYTE*)val);*/
                        write_var(m, val, valsz, v);
                    }
                    break;
            }
            continue;
        }
        switch(mode) {
            case SEARCH:
                if (!populate_mem_map_opt(m, 1, 1, 1)) {
                    puts("failed to (re)populate");
                }
                val = str_to_val(ln, ln_len, &valsz, &frame->current_type);
                if (val) {
                    // hmm, narrowing after a string search is broken
                    // AH! it's because len is unequal! for strings we'll need a special case
                    c_st = clock();
                    narrow_mem_map_frame_opt(m, frame, n_threads, val, valsz);
                    c_end = clock();
                    printf("narrowed down to %i values with %i threads per region (%f secs)\n", 
                           frame->n_tracked, n_threads, ((double)(c_end - c_st)) / CLOCKS_PER_SEC);
                }
                break;
            case EDIT:
                puts("edit");
                break;
        }
        free(ln);
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

      // for testing - inserting this right after grabbing pid
      /*
       * find_var(pid);
       * return 0;
      */

      // default to stack, heap and additional if no region is specified
      if(d_rgn == NONE && !additional){
            d_rgn = BOTH;
            additional = true;
      }

      struct mem_map_optimized mm;
      init_mem_map_opt(&mm);
      // TODO: this should probably be added to init function
      add_frame(&mm, "DEFAULT");
      mm.rgn = get_vmem_locations(pid, 1);

      struct mem_map vmem;
      mem_map_init(&vmem, pid, unmarked);
      // TODO: fix criteria for unmarked additional mem rgns in vmem_parser.c, too many regions are being recorded
      // no warnings are printed unless we're in interactive mode
      if(!mem_rgn_warn(d_rgn, mm.rgn, additional, verbose || mode != 'i')){
            puts("no usable memory regions found\nyou DO have root privileges, don't you");
            free_mem_rgn(&mm.rgn);
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
            if (interactive_mode_opt(&mm)) {
                free_mem_map_opt(&mm);
            }
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
      free_mem_rgn(&mm.rgn);
      // not_run will be non-zero when an error has been found
      return not_run;
}
