#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

#ifdef shared
#include <vmem_access.h>
#else
#include "vmem_access.h"
#endif
#include "frame_dump.h"

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

bool mem_rgn_warn(enum m_region d_rgn, struct mem_rgn mem, bool silent){
      bool no_ad = (mem.n_remaining == 0 && (d_rgn & OTHER));
      int viable_rgns = 3;

      if(no_ad){
            if(!silent)fputs("WARNING: no valid unmarked memory regions were found\n", stderr);
            --viable_rgns;
      }
      if((d_rgn & STACK) && (mem.stack.start == NULL || mem.stack.end == NULL)){
            if(!silent)fputs("WARNING: no valid stack memory region was found\n", stderr);
            --viable_rgns;
      }
      if((d_rgn & HEAP) && (mem.heap.start == NULL || mem.heap.end == NULL)){
            if(!silent)fputs("WARNING: no valid heap memory region was found\n", stderr);
            --viable_rgns;
      }
      return (bool)viable_rgns;
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
    write_bytes_to_pid_mem(m->rgn.pid, valsz, (void*)get_remote_addr(m, v), (uint8_t*)val);
}

char* get_frame_arg(char* full_arg, int arglen) {
    int idx = 4;
    if (arglen <= 4) {
        return NULL;
    }

    for (; idx < arglen && isspace(full_arg[idx]); ++idx);

    return full_arg + idx;
}

void frame_operation(struct mem_map_optimized* m, struct narrow_frame** current_frame, char* arg, int arglen) {
    void* write_val;
    struct narrow_frame* tmp_f;
    uint16_t valsz;
    char* frame_arg;

    struct framedump fd_test;

    frame_arg = get_frame_arg(arg, arglen);

    switch(arg[2]) {
        case 'D':
            fill_framedump(&fd_test, m, *current_frame);
            break;
        // /fr rename current frame, helpful for using DEFAULT frame
        case 'r':
            if (frame_arg) {
                strncpy((*current_frame)->label, frame_arg, sizeof((*current_frame)->label));
            }
            break;
        // /fh update frame history limit!
        case 'h':
            break;
        // /fc frame create
        case 'c':
            if (frame_arg) {
                add_frame(m, frame_arg);
            }
            break;
        // /fs frame select
        case 's':
            if (frame_arg) {
                tmp_f = frame_search(m, frame_arg);
                if (tmp_f) {
                    printf("current frame set to \"%s\" with %i tracked vars\n", tmp_f->label, tmp_f->n_tracked);
                    *current_frame = tmp_f;
                }
            }
            break;
        // /fw frame write
        case 'w':
            if (frame_arg) {
                write_val = str_to_val(frame_arg, arglen - (frame_arg - arg), &valsz, &(*current_frame)->current_type);
                for (struct found_variable* v = (*current_frame)->tracked_vars; v; v = v->next) {
                    write_var(m, write_val, valsz, v);
                }
            }
            break;
        default:
        // /fl frame list
        case 'l':
            printf("%i frames, \"**\" indicates current frame\n", m->n_frames);
            for (struct narrow_frame* f = m->frames; f; f = f->next) {
                printf("%s\"%s\" - tracking %i %s variables\n", (*current_frame == f) ? "** " : "", f->label, f->n_tracked, type_to_str(f->current_type));
            }
            break;
    }
}

_Bool interactive_mode_opt(struct mem_map_optimized* m) {
    char* ln = NULL;
    ssize_t ln_len;
    enum i_mode mode = SEARCH;
    uint8_t n_threads = 3;
    struct narrow_frame* frame = m->frames;

    void* val;
    uint16_t valsz;

    // used for freeing frame contents during a /reset
    struct narrow_frame dummy_f;

    struct timespec c_st, c_end;
    double elapsed;

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
        //  TODO: don't use additional regions by default!
        //
        //  okay, it's time to write frame operations! like frame write with args
        //  they all need args - frame write, switch frame, yay!
        if (*ln == '/') {
            switch(ln[1]) {
                case 'q':
                    return 1;
                // reset
                case 'r':
                    {
                    dummy_f.tracked_vars = frame->tracked_vars;
                    dummy_f.n_tracked = frame->n_tracked;
                    free_frame(&dummy_f);

                    frame->tracked_vars = NULL;
                    frame->n_tracked = 0;
                    frame->current_type = NONE_T;

                    }
                    break;
                case 'U':
                    undo_renarrow(frame);
                    printf("restored frame to %i variables\n", frame->n_tracked);
                    break;
                case 'f':
                    frame_operation(m, &frame, ln, ln_len);
                    break;
                case 's':
                    mode = SEARCH;
                    break;
                case 'e':
                    mode = EDIT;
                    break;
                case 'u':
                    populate_mem_map_opt(m);
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
                        /*write_bytes_to_pid_mem(m->rgn.pid, valsz, (void*)get_remote_addr(m, v), (uint8_t*)val);*/
                        write_var(m, val, valsz, v);
                    }
                    break;
            }
            continue;
        }
        switch(mode) {
            case SEARCH:
                if (!populate_mem_map_opt(m)) {
                    puts("failed to (re)populate");
                }
                val = str_to_val(ln, ln_len, &valsz, &frame->current_type);
                if (val) {
                    // hmm, narrowing after a string search is broken
                    // AH! it's because len is unequal! for strings we'll need a special case
                    clock_gettime(CLOCK_MONOTONIC, &c_st);
                    narrow_mem_map_frame_opt(m, frame, n_threads, val, valsz);
                    clock_gettime(CLOCK_MONOTONIC, &c_end);
                    elapsed = c_end.tv_sec - c_st.tv_sec + ((c_end.tv_nsec - c_st.tv_nsec) / 1000000000.0);
                    printf("narrowed down to %i values with %i threads per region (%f secs)\n", 
                           frame->n_tracked, n_threads, elapsed);
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
      bool integers=true, verbose=false, print_rgns=false;
      (void)print_rgns;
      enum m_region d_rgn = NONE;
      int n_bytes=4, result_print_limit=100;
      // stores argv index of previous value setting argument
      int p = -1;
      // default to interactive mode
      char mode = 'i';
      // stores argument indices of argv for p, r, w modes
      int args[2];
      pid_t pid = 0;
      struct mem_map_optimized mm;

      for(int i = 1; i < argc; ++i){
            if(*argv[i] == '-'){
                  // if strlen == 2
                  if(argv[i][1] && !argv[i][2]){
                        switch(argv[i][1]){
                              case 'S': d_rgn |= STACK; break;
                              case 'H': d_rgn |= HEAP; break;
                              case 'B': d_rgn |= STACK | HEAP; break;
                              case 'A': d_rgn |= OTHER; break;
                              case 'E': d_rgn = STACK | HEAP | OTHER; break;
                              case 'C': integers = false; break;
                              case 'b': if(!(argc > i+1) || !strtoi(argv[i+1], NULL, &n_bytes) || n_bytes == 0 || n_bytes > 4)n_bytes = 4; else if(p != -2)p = i+1; break;
                              case 'V': verbose = true; print_rgns = true; break;
                              case 'v':{
                                          printf("%s using %s\n", ver, MEMCARVE_VER); return -1;
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
      if(d_rgn == NONE) {
            d_rgn = STACK | HEAP | OTHER;
      }

      init_mem_map_opt(&mm, d_rgn);
      // TODO: this should probably be added to init function
      add_frame(&mm, "DEFAULT");
      mm.rgn = get_vmem_locations(pid, 1);

      // TODO: fix criteria for unmarked additional mem rgns in vmem_parser.c, too many regions are being recorded
      // no warnings are printed unless we're in interactive mode
      if(!mem_rgn_warn(d_rgn, mm.rgn, verbose || mode != 'i')){
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
                        uint8_t to_w[n_bytes];
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
      // TODO: modernize this for new vmem_access system - this no longer needs to be limited to int/string
      // we can use get_val() or similar and print any type
      else if(mode == 'p'){
            // TODO: use d_rgn to populate mm
            populate_mem_map_opt(&mm);
            // search strings beginning with '-' must be escaped with '\'
            // TODO: document this
            // if we're going to try to narrow
            if(argc > args[0] && *argv[args[0]] != '-'){
                  char* filt = argv[args[0]];
                  filt += *filt == '\\';
                  if(!integers)narrow_mem_map_frame_opt(&mm, mm.frames, 1, filt, strlen(filt));
                  else if(strtoi(filt, NULL, &args[1]))narrow_mem_map_frame_opt(&mm, mm.frames, 1, &args[1], sizeof(int));
            }
            if (integers) {
                p_frame_var(&mm, mm.frames, "i", int);
            } else {
                p_frame_var(&mm, mm.frames, "s", char*);
            }
            free_mem_map_opt(&mm);
      }
      if(not_run == 1 || not_run == 3)puts("enter a valid address");
      if(not_run == 2 || not_run == 3)puts("enter a valid integer to write");
      free_mem_rgn(&mm.rgn);
      // not_run will be non-zero when an error has been found
      return not_run;
}
