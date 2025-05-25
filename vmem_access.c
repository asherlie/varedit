/*TODO: can test this by having a file with letters and searching for 97b for 'a' for example!*/
#include "vmem_access.h"

#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <assert.h>
#include <sys/uio.h>
#include <sys/mman.h>

bool read_bytes_from_pid_mem_dir(void* dest, pid_t pid, int bytes, void* vm_s, void* vm_e){
      int sz_rgn;
      if(vm_e == NULL)sz_rgn = bytes;
      else sz_rgn = (char*)vm_e-(char*)vm_s;
      struct iovec local;
      struct iovec remote;
      local.iov_base = dest;
      local.iov_len = sz_rgn;
      remote.iov_base = vm_s;
      remote.iov_len = sz_rgn;
      ssize_t nread;
      nread = process_vm_readv(pid, &local, 1, &remote, 1, 0);
      /*printf("read %li bytes - %p -> %p\n", nread, vm_s, vm_e);*/
      return nread == sz_rgn;
}

uint8_t* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e){
      int sz_rgn;
      if(vm_e == NULL)sz_rgn = bytes;
      else sz_rgn = (char*)vm_e-(char*)vm_s;
      uint8_t* ret = malloc(sz_rgn);
      read_bytes_from_pid_mem_dir(ret, pid, bytes, vm_s, vm_e);
      return ret;
}

int read_single_val_from_pid_mem(pid_t pid, int bytes, void* vm){
      int ret = 0;
      read_bytes_from_pid_mem_dir(&ret, pid, bytes, vm, NULL);
      return ret;
}

char* read_str_from_mem_range(pid_t pid, void* mb_start, int len){
      return (char*)read_bytes_from_pid_mem(pid, 1, mb_start, (void*)((char*)mb_start+len));
}

/* when dest == NULL, a malloc'd string is returned
 * min_strlen can safely be set to 0 and last_avail to NULL with an efficiency tradeoff
 * THIS FUNCTION SHOULD ONLY BE USED WHEN STRING SIZE IS UNKNOWN
 */ 
char* read_str_from_mem_range_slow_dir(char* dest, pid_t pid, void* mb_start, int min_strlen, void* last_avail){
      char tmp;
      int str_sz = min_strlen+5;
      char* ret = dest;
      if(!dest)ret = malloc(sizeof(char)*str_sz+1);
      int ret_p = 0;
      if(min_strlen > 0)read_bytes_from_pid_mem_dir(ret, pid, min_strlen, mb_start, NULL);
      for(char* i = (char*)mb_start+min_strlen; i != last_avail; ++i){
            tmp = read_single_val_from_pid_mem(pid, 1, (void*)i);
            if(!(tmp > 0 && tmp < 127))return ret;
            if(!dest && ret_p == str_sz){
                  str_sz *= 2;
                  char* tmp_ret = malloc(sizeof(char)*str_sz+1);
                  memset(tmp_ret+ret_p, '\0', str_sz-ret_p);
                  strcpy(tmp_ret, ret);
                  free(ret);
                  ret = tmp_ret;
            }
            ret[ret_p++] = tmp;
      }
      return ret;
}

char* read_str_from_mem_range_slow(pid_t pid, void* mb_start, void* mb_end){
      return read_str_from_mem_range_slow_dir(NULL, pid, mb_start, 0, mb_end);
}

bool pid_memcpy(pid_t dest_pid, pid_t src_pid, void* dest, void* src, int n_bytes){
      bool ret = true;
      // don't use read_bytes_from_pid_mem to read from current process
      uint8_t* bytes = (src_pid == getpid()) ? (uint8_t*)src : read_bytes_from_pid_mem(src_pid, n_bytes, src, NULL);
      // don't use write_bytes_to_pid_mem to write to current process
      if(dest_pid == getpid())memcpy(dest, bytes, n_bytes);
      else ret = write_bytes_to_pid_mem(dest_pid, n_bytes, dest, bytes);
      if(src_pid != getpid())free(bytes);
      return ret;
}

bool write_bytes_to_pid_mem(pid_t pid, int bytes, void* vm, uint8_t* value){
      struct iovec local;
      struct iovec remote;
      local.iov_base = value;
      local.iov_len = bytes;
      remote.iov_base = vm;
      remote.iov_len = bytes;
      return (bytes == process_vm_writev(pid, &local, 1, &remote, 1, 0));
}

bool write_int_to_pid_mem(pid_t pid, void* vm, int value){
      uint8_t byte_int[4];
      memcpy(byte_int, &value, 4);
      return write_bytes_to_pid_mem(pid, 4, vm, byte_int);
}

bool write_str_to_pid_mem(pid_t pid, void* vm, const char* str){
      return write_bytes_to_pid_mem(pid, strlen(str), vm, (uint8_t*)str);
}

/* ~~~~~~~~~~~~~~~~begin optimized feb 2025 changes~~~~~~~~~~~~~~~~~ */

void init_frames(struct mem_map_optimized* m) {
    m->n_frames = 0;
    m->frames = NULL;
    // this must be zeroed for NULL first frame
}

void init_mem_map_opt(struct mem_map_optimized* m, enum m_region rgn) {
    m->stack = m->heap = NULL;
    m->other = NULL;
    /*m->mapped_disk = NULL;*/
    m->selected_rgns = rgn;

    m->n_disk = 0;
    m->disk_fns = NULL;
    m->disk_mapped = 0;

    init_frames(m);
}

void free_frame(struct narrow_frame* frame) {
    struct found_variable* prev_v = NULL;
    for (struct found_variable* v = frame->tracked_vars; v; v = v->next) {
        if (prev_v) {
            free(prev_v);
        }
        prev_v = v;
    }
    free(prev_v);
}

void free_mem_map_opt(struct mem_map_optimized* m) {
    struct narrow_frame* prev_f = NULL;
    if (m->stack) {
        free(m->stack);
    }
    if (m->heap) {
        free(m->heap);
    }

    if (m->other) {
        for (int i = 0; i < m->rgn.n_remaining - m->n_disk; ++i) {
            free(m->other[i]);
        }
    }

    for (struct narrow_frame* f = m->frames; f; f = f->next) {
        free_frame(f);
        if (prev_f) {
            free(prev_f);
        }
        prev_f = f;
    }
}

struct narrow_frame* frame_search(struct mem_map_optimized* m, char* str) {
    for (struct narrow_frame* f = m->frames; f; f = f->next) {
        if (strstr(f->label, str)) {
            return f;
        }
    }
    return NULL;
}

uint64_t rgn_len(struct m_addr_pair* addrs) {
    return ((uint8_t*)addrs->end - (uint8_t*)addrs->start);
}

// returns file descriptor, sets sz
int filesz(char* fn, size_t* sz) {
    int fd = open(fn, O_RDWR, S_IRWXU);

    if (fd == -1) {
        return -1;
    }

    *sz = lseek(fd, 0, SEEK_END);

    if (*sz <= 0) {
        return -1;
    }

    lseek(fd, 0, SEEK_SET);

    return fd;
}

void add_disk_fn(struct disk_map_inf** dmi, char* fn) {
    struct disk_map_inf* new_dmi = malloc(sizeof(struct disk_map_inf));
    new_dmi->fn = fn;
    new_dmi->sz = -1;
    new_dmi->next = NULL;
    if (!*dmi) {
        *dmi = new_dmi;
        return;
    }
    new_dmi->next = *dmi;
    *dmi = new_dmi;
}

// ugh, maybe can't decrement n_disk if it's used to go from files here
// ok, the reader will know if they've gotten to the end if they've reached n_disk - 1 OR a NULL entry
int populate_mem_map_opt_disk(struct mem_map_optimized* m) {
    int fd;
    int err = 0;
    // realloc()ing m->other 
    if (m->disk_mapped) {
        for (struct disk_map_inf* dm = m->disk_fns; dm; dm = dm->next) {
            printf("msync() returned %i\n", msync(dm->address, dm->sz, MS_SYNC));
            // okay, updating this to just munmap() and re-map. the above isn't working.
            // removing the else condition below
            // OKAY - seems to fix problem! not a great solution but okay for now
            munmap(dm->address, dm->sz);
        }
    } /* else { */
        /*replace mapped_disk with pointers into m->disk_fns.address*/
        /*m->mapped_disk = calloc(m->n_disk, sizeof(uint8_t*));*/

        for (struct disk_map_inf* dm = m->disk_fns; dm; dm = dm->next) {
        /*for (uint16_t i = 0; i < m->n_disk; ++i) {*/
            // TODO: can i close this FD?
            fd = filesz(dm->fn, &dm->sz);
            if (fd == -1) {
                ++err;
                continue;
            }
            // set disk_mapped to 1 if we have any valid disk files
            m->disk_mapped = 1;
            // TODO: if this doesn't work it's likely a page size issue - may need to use multiples of page size
            dm->address = mmap(0, dm->sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            printf("address to %p\n", dm->address);
        }
        printf("subtracting %i errs from n_disk\n", err);
        /*m->rgn.n_remaining += m->n_disk;*/
    // }
    return m->n_disk - err;
}

// TODO: potentially only repopulate regions that have matches in frame
// TODO: this should fail when process shuts down
// TODO: detect failures from initial population
// this is called only once for both disk regions and traditional regions
_Bool populate_mem_map_opt_memory(struct mem_map_optimized* m) {
    // we'll assume caller sets m->rgn
    /*m->rgn = get_vmem_locations(pid, unmarked_additional);*/
    // using a byte value of 32 - hoping it'll be faster and shouldn't matter for actual data now that
    // we're lumping it all together
    int bytes = 32;
    int failures = (m->selected_rgns & STACK) + (m->selected_rgns & HEAP) + (m->selected_rgns & OTHER) ? m->rgn.n_remaining : 0;
    if (m->selected_rgns & STACK) {
        if (m->stack) {
            /*memset(m->stack, 0, rgn_len(&m->rgn.stack));*/
            /*uint8_t* tmp_stack = read_bytes_from_pid_mem(m->rgn.pid, bytes, m->rgn.stack.start, m->rgn.stack.end);*/
            /*memcpy(m->stack, tmp_stack, rgn_len(&m->rgn.stack));*/
            if (!read_bytes_from_pid_mem_dir(m->stack, m->rgn.pid, bytes, m->rgn.stack.start, m->rgn.stack.end)) {
                --failures;
            }
            /*printf("inserted stack bytes into %p -> %p\n", (void*)m->stack, m->stack + ((uint8_t*)m->rgn.stack.end - (uint8_t*)m->rgn.stack.end));*/
        } else {
            m->stack = read_bytes_from_pid_mem(m->rgn.pid, bytes, m->rgn.stack.start, m->rgn.stack.end);
            --failures;
        }
    }
    if (m->selected_rgns & HEAP) {
        if (m->heap) {
            if (read_bytes_from_pid_mem_dir(m->heap, m->rgn.pid, bytes, m->rgn.heap.start, m->rgn.heap.end)) {
                --failures;
            }
        } else {
            m->heap = read_bytes_from_pid_mem(m->rgn.pid, bytes, m->rgn.heap.start, m->rgn.heap.end);
            --failures;
        }
    }
    if (m->selected_rgns & OTHER) {
        if (!m->other) {
            m->other = calloc(m->rgn.n_remaining, sizeof(uint8_t*));
        }
        for (uint8_t i = 0; i < m->rgn.n_remaining; ++i) {
            if (m->other[i]) {
                if (!read_bytes_from_pid_mem_dir(m->other[i], m->rgn.pid, bytes, m->rgn.remaining_addr[i].start,
                                                 m->rgn.remaining_addr[i].end)) {
                    --failures;
                }
            } else {
                m->other[i] = read_bytes_from_pid_mem(m->rgn.pid, bytes, m->rgn.remaining_addr[i].start, m->rgn.remaining_addr[i].end);
                --failures;
            }
        }
    }
    return !failures;
}


_Bool populate_mem_map_opt(struct mem_map_optimized* m) {
    _Bool ret;
    // memory mmo is populated first to ensure that the initial additional regions are dedicated to memory
    ret = populate_mem_map_opt_memory(m);
    populate_mem_map_opt_disk(m);
    return ret;
}

// TODO: write this code - should be a linked list. no need to be threadsafe, this will only be called
// from the main thread in varedit and during frame operations in dump restores
void add_frame(struct mem_map_optimized* m, char* label) {
    struct narrow_frame* f = malloc(sizeof(struct narrow_frame));
    f->n_tracked = 0;
    strncpy(f->label, label, sizeof(f->label) - 1);
    f->next = m->frames;
    f->tracked_vars = NULL;
    f->current_type = NONE_T;
    /* TODO: have this set by user and make it default to a low number to save memory */
    f->undo_depth_limit = INT_MAX;
    f->undo_depth = 0;
    f->earliest_hist = f->latest_hist = NULL;
    m->frames = f;
    ++m->n_frames;
}

/*
 * could just insert replacing frame_var with a copy that has a prev value
*/
/*is this really threadsafe, seems not to be. */
void insert_frame_var(struct narrow_frame* frame, uint8_t* address, uint8_t len) {
    struct found_variable* var = malloc(sizeof(struct found_variable));
    var->address = address;
    var->len = len;

    var->next = frame->tracked_vars;

    while (1) {
        // ah, just understood why they do this for me. no need for updating this
        // weirdly it crashes less often when i explicitly set var->next each iteration
        // TODO: investigate if this is still a problem after fixing corrupted arg memory
        /*var->next = frame->tracked_vars;*/
        if (atomic_compare_exchange_strong(&frame->tracked_vars, &var->next, var)) {
            atomic_fetch_add(&frame->n_tracked, 1);
            break;
        }
        // too many wasted attempts are slowing down multithreaded performance
        // AH! i have a great idea! each thread can build up its own linked list and only combine them
        // at the end!
        // this avoids the need for threadsafe insertion until the very end!!
        /*puts("FAIled");*/
    }
    /*printf("succesfully inserted a new frame var");*/
}

// inserts a tracked_vars linked list into a frame
void combine_frame_var(struct narrow_frame* frame, struct found_variable* vars, struct found_variable* vars_last, int n_vars) {
    vars_last->next = frame->tracked_vars;
    while (1) {
        if (atomic_compare_exchange_strong(&frame->tracked_vars, &vars_last->next, vars)) {
            atomic_fetch_add(&frame->n_tracked, n_vars);
            break;
        }
        puts("FAILED");
    }
}

void init_narrow_history(struct narrow_history* h) {
    h->next = NULL;
    h->removed = NULL;
    /*h->last = NULL;*/
    h->n_removed = 0;
}

#if 0
okay, THINK THROUGH THIS CODE. something about history FRAME order is wrong
the combining of frames seems to be working as expected!
#endif
// adds a new history period, removes oldest if we've reached our limit
// earliest->next->next->latest
struct narrow_history* add_history_search(struct narrow_frame* frame) {
    struct narrow_history* ret = NULL;
    if (!frame->earliest_hist) {
        ++frame->undo_depth;
        ret = frame->earliest_hist = malloc(sizeof(struct narrow_history));
    }
    else if (frame->undo_depth == frame->undo_depth_limit) {
        ret = frame->earliest_hist;
        frame->earliest_hist = frame->earliest_hist->next;
        frame->latest_hist->next = ret;
    } else {
        ++frame->undo_depth;
        ret = frame->latest_hist->next = malloc(sizeof(struct narrow_history));
    }

    frame->latest_hist = ret;
    init_narrow_history(ret);
    return ret;
}

/*found the problem, earliest_hist, latest_hist are IDENTICAL despite count being 3!!!*/
struct narrow_history* pop_latest_hist(struct narrow_frame* frame) {
    struct narrow_history* ret = frame->latest_hist, * prev = NULL;

    // TODO: is there anything else needed to handle this?
    printf("entered pop_latest_hist(), earliest: %p, latest: %p, N: %i\n", (void*)frame->earliest_hist, (void*)frame->latest_hist, frame->undo_depth);
    if (!ret) {
        return NULL;
    }

    --frame->undo_depth;

    if (frame->earliest_hist == frame->latest_hist) {
        ret = frame->earliest_hist;
        frame->earliest_hist = frame->latest_hist = NULL;
        return ret;
    }

    for (prev = frame->earliest_hist; prev; prev = prev->next) {
        if (prev->next == ret) {
            prev->next = ret->next;
            break;
        }
    }

    // no need for NULL check i believe
    frame->latest_hist = prev;
    if (!frame->latest_hist) {
        puts("ZEROED latest");
    }
// repeat the many narrow and undo test where altest is printed, seems to be a bit weird
    if (frame->latest_hist == frame->earliest_hist) {
        puts("set altest to earliest");
    }
    /* is latest / earliest not being set properly? - this should be done when we empty the list and before
     * this could be because we're not moving pointers properly
     */

    return ret;
}

void insert_history(struct narrow_history* h, struct found_variable* v) {
    ++h->n_removed;
    v->next = NULL;
    if (!h->removed) {
        printf("inserting first entry into %p\n", (void*)h);
        h->last = v;
        h->removed = v;
        return;
    }
    h->last->next = v;
    h->last = v;
}

// returns v->next if !free
struct found_variable* rm_next_frame_var_unsafe(struct narrow_frame* frame, struct found_variable* v, _Bool rm_first, struct narrow_history* hist) {
    struct found_variable* to_free;
    --frame->n_tracked;
    if (!v && rm_first) {
        to_free = frame->tracked_vars;
        frame->tracked_vars = frame->tracked_vars->next;
        // okay, this is causing segfault... WHY?
        /*free(to_free);*/

        // commented out to try out new system where this func adds to hist
        /*return to_free;*/
    } else {

    /*v == NULL at some point*/
        to_free = v->next;
        v->next = v->next->next;
    }

    if (hist) {
        insert_history(hist, to_free);
    } else {
        free(to_free);
        to_free = NULL;
    }

    return to_free;
}

// this version uses locks as a proof of concept and sets prev
/*first element must have next = NULL;*/
// must init frame to NULL!

char* get_disk_fn(struct mem_map_optimized* m, struct found_variable* v, size_t* offset) {
    for (struct disk_map_inf* dm = m->disk_fns; dm; dm = dm->next) {
        if (v->address > dm->address && v->address < dm->address + dm->sz) {
            *offset = v->address - dm->address;
            return dm->fn;
        }
    }
    return NULL;
}

/*
 * calculates address given two ranges and a local address
 * it saves memory and time to only calculate this once we're printing out narrowed
 * variables, the cost of looking this up for each variable is insignificant once we've
 * sufficiently narrowed
*/
// TODO: this can easily be updated to return which region we're working with
// so it can be printed for user per variable
uint8_t* get_remote_addr(struct mem_map_optimized* m, struct found_variable* v) {
    uint8_t* local_rgn_end = NULL;
    struct m_addr_pair* remote_rgn;
    if (v->address > m->stack && v->address < (m->stack + rgn_len(&m->rgn.stack))) {
        /*mem_rgn = m->stack;*/
        remote_rgn = &m->rgn.stack;
        local_rgn_end = m->stack + rgn_len(&m->rgn.stack);
    } else if (v->address > m->heap && v->address < (m->heap + rgn_len(&m->rgn.heap))) {
        /*mem_rgn = m->heap;*/
        remote_rgn = &m->rgn.heap;
        local_rgn_end = m->heap + rgn_len(&m->rgn.heap);
    } else {
        for (int i = 0; i < m->rgn.n_remaining; ++i) {
            if (v->address > m->other[i] && v->address < (m->other[i] + rgn_len(&m->rgn.remaining_addr[i]))) {
                /*mem_rgn = m->other[i];*/
                remote_rgn = &m->rgn.remaining_addr[i];
                local_rgn_end = m->other[i] + rgn_len(&m->rgn.remaining_addr[i]);
            }
        }
    }

    if (!local_rgn_end)return NULL;

    return (uint8_t*)remote_rgn->end - (local_rgn_end - v->address);
}

/*should i write a feature that keeps a list of addresses for a given process name - it can do this based on the variable offsets from start of region*/
/*TODO: fix up readme*/
/*
 * yes - i'll call it /fd - frame dump - dumps all current frames to a file, they'll be stored in relative offsets to a region!
 * this will hopefully enable loading a dump and immediately editing variables!
*/
// for debugging, prints a full frame including pointers to see what's getting corrupted. use before and after removal.
void _p_frame_var(struct mem_map_optimized* m, struct narrow_frame* frame) {
    for (struct found_variable* v = frame->tracked_vars; v; v = v->next) {
        /*printf("%", v->);*/
        printf("%p\n", get_remote_addr(m, v));
    }
}


// searches from start -> end for regions of size valsz with value `value`. adds to frame when found
// TODO: this shouldn't use locks - make frame struct a lock free linked list
/*
 * need a way to link frame vars with real remote addresses so we can narrow multiple times
 * maybe remove the repopulation logic above. i just need to read_bytes_from_mem on a much smaller scale after initial frame generation
 * maybe populate_mem_map_opt() should create a frame implicitly!
 * actually prob better if it just exists seperately. i can just create as many frames as i want separately
 * 
 * omg wait this is already so elegant! 
 * 1: we grab memory with populate()
 * 2: narrow() , let user know which regions they should reupdate
 * 3: repopulate relevant regions
 * 4: iterate through frame, pointers will have updated values automatically!
*/

// this is used only for initial narrow! after this, we can just inspect our linked list
// TODO: rename this function to be create frame or similar - that's what it does. renarrow() should be narrow

// okay, when i search for a nonexistent variable, each thread returns 1 result! this is my issue.
uint64_t narrow_mem_map_frame_opt_subroutine(struct narrow_frame* frame, uint8_t* start_rgn, uint8_t* end_rgn, void* value, uint16_t valsz) {
    uint64_t n_matches = 0;
    uint8_t* first_byte_match = start_rgn;
    uint8_t* i_rgn = start_rgn;

    struct found_variable* found = NULL, * last = NULL, * tmp_v;

    // this is only necessary on the first pass - we can know this by n_tracked!
    // until we exhaust all matches of first byte
    /*for (; first_byte_match; first_byte_match = memmem(i_rgn, end_rgn - i_rgn, value, valsz)) {*/
    /* NOTE: memmem() was tried, but resulted in flase positives due to broken memmem() implementation */
    const uint8_t first_byte = *(uint8_t*)value;
    /*printf("thread doing %p -> %p\n", (void*)start_rgn, (void*)end_rgn);*/
    for (; first_byte_match; first_byte_match = memchr(i_rgn, first_byte, end_rgn - i_rgn)) {
        if (first_byte_match + valsz > end_rgn) {
            /*ALSO need to exit once first_byte_match is not in our defined region. could cause concurrency issues.*/
            /*
             * is failing to do this check causing duplicates?
             * what if a value is on the boundary of two regions? doubt it
            */
            break;
            /*return n_matches;*/
        }
        if (!memcmp(first_byte_match, value, valsz)) {
            /*printf("inc'd n_matches for %i\n", memcmp(first_byte_match, value, valsz));*/
            ++n_matches;

            tmp_v = malloc(sizeof(struct found_variable));
            tmp_v->address = first_byte_match;
            tmp_v->len = valsz;

            if (!found) {
                tmp_v->next = NULL;
                /*last = found = tmp_v;*/
                last = tmp_v;
            } 
            tmp_v->next = found;
            found = tmp_v;

            /*insert_frame_var(frame, first_byte_match, valsz);*/
            i_rgn = first_byte_match + valsz;
        } else {
            i_rgn = first_byte_match + 1;
        }

        // TODO: i removed the check for i_rgn being between end_rgn, start_rgn - does this need
        // to be added back? I don't believe so because the above check should cover that
    }
    if (found) {
        /*printf("combining %li matches into frame - found: %p, last: %p\n", n_matches, (void*)found, (void*)last);*/
        combine_frame_var(frame, found, last, n_matches);
        printf("calling combine_frame_var() with %li matches\n", n_matches);
    }
    return n_matches;
}

struct narrow_mmo_sub_args{
    struct narrow_frame* frame;
    uint8_t* start_rgn;
    uint8_t* end_rgn;
    void* value;
    uint16_t valsz;
};

void* narrow_mmo_sub_wrapper(void* varg) {
    struct narrow_mmo_sub_args* arg = varg;
    narrow_mem_map_frame_opt_subroutine(arg->frame, arg->start_rgn, arg->end_rgn, arg->value, arg->valsz);
    free(arg);
    return NULL;
}

// i don't believe there's any concurrency risk with iterating over this simply
// only one thread will be doing the renarrowing and this is strictly later than initial narrow occurs
// the only concurrent section of code is the original narrowing
// WAIT... does this mean that we don't even need frame var REMOVAL to be threadsafe? LOL
// only insertion must be! and that's the easy part. wonderful.
//
// OKAY, SO:
//  1. keep insertion to be lock free
//  2. update renarrow next to not be threadsafe with current logic
//  3. make note that this function is not threadsafe
//
void renarrow_frame(struct narrow_frame* frame, void* value, uint16_t valsz) {
    _Bool removed = 0;
    /*struct found_variable* hist_append;*/
    struct narrow_history* hist = NULL;

    // TODO: possibly move history creation to add_frame() - if it cannot be set with a command that is
    if (frame->undo_depth_limit != 0) {
        // add a history period for this renarrow
        hist = add_history_search(frame);
        printf("renarrow() got %p to add hist to\n", (void*)hist);
    }

    for (struct found_variable* v = frame->tracked_vars; v && v->next; v = (removed) ? v : v->next) {
        // TODO: this may not hold true when it comes to strings
        //       for strings, scrap this assertion and memcmp with the smaller size
        /*assert(valsz == v->next->len);*/

        removed = 0;
        if (memcmp(v->next->address, value, valsz)) {
            /*instead of this, insert it into undo!!*/
            // TODO: free up this memory in free_frame() AND whenever we apply an undo!
            rm_next_frame_var_unsafe(frame, v, 0, hist);
            removed = 1;
        }
    }

    /* check first element */
    /*assert(valsz == frame->tracked_vars->len);*/
    if (frame->tracked_vars && memcmp(frame->tracked_vars->address, value, valsz)) {
        rm_next_frame_var_unsafe(frame, NULL, 1, hist);
    }
}

// undoes most recent renarrow() - does nothing if there is no available history
/*void undo_renarrow(struct narrow_frame* frame) {*/
void undo_renarrow(struct narrow_frame* frame) {
    struct narrow_history* hist;

    if (!frame->undo_depth_limit || !frame->undo_depth || !(hist = pop_latest_hist(frame)) || !hist->removed) {
        /*printf*/
        return;
    }
    // TODO: no need to use combine_frame_var() - this is not threadsafe
    /*combine_frame_var();*/
    /*frame->latest_hist*/
    /*frame->tracked_vars*/
    /*hist = pop_latest_hist(frame);*/
    hist->last->next = frame->tracked_vars;
    frame->tracked_vars = hist->removed;
    frame->n_tracked += hist->n_removed;
}

/* either calls directly OR spawns n_threads to run a narrow */
/* 0 is only passed in if regions are to be narrowed sequentially - NOT RECOMMENDED */
// TODO: should smaller regions be searched in one thread?
pthread_t* narrow_mmo_sub_spawner(struct narrow_frame* frame, uint8_t n_threads, uint8_t* start,
                                  uint8_t* end, void* value, uint16_t valsz) {
    uint64_t addr_per_th;
    uint8_t* running_start = start;
    struct narrow_mmo_sub_args* targ;

    /* malloc(0) can be free()d - see man 3 malloc */
    pthread_t* pth = malloc(sizeof(pthread_t) * n_threads);

    if (n_threads == 0) {
        narrow_mem_map_frame_opt_subroutine(frame, start, end, value, valsz);
        free(pth);
        return NULL;
    }

    addr_per_th = (end - start) / n_threads;
    /* it is not necessary for addr_per_th to be a multiple of valsz
     * if the first byte of a match is found by a narrow thread and the rest of that match
     * lies on the other side of a boundary, the thread will still be okay to insert that value into
     * the frame. this is perfectly safe, as no other thread will insert this duplicate
     * since its first byte is behind their region's start. also, no writes are made to the buffer during
     * initial narrowing.
     */
    /*printf("%i threads for NEW REGION: %p -> %p (%li)\n", n_threads, (void*)start, (void*)end, end - start);*/
    for (uint8_t i = 0; i < n_threads; ++i) {
        targ = malloc(sizeof(struct narrow_mmo_sub_args));
        targ->frame = frame;
        targ->start_rgn = running_start;
        targ->value = value;
        targ->valsz = valsz;
        targ->end_rgn = targ->start_rgn + addr_per_th;

        /* in case of truncation during division, make sure to reach the end of memory region in last thread */
        if (i == n_threads - 1) {
            targ->end_rgn = end;
        }
        /*printf("thread %i: %p -> %p (%li)\n", i, (void*)targ->start_rgn, (void*)targ->end_rgn, targ->end_rgn - targ->start_rgn);*/
        pthread_create(pth + i, NULL, narrow_mmo_sub_wrapper, targ);

        running_start = targ->end_rgn;
    }
    return pth;
}

/* narrows variables in memory of all  */
void narrow_mem_map_frame_opt(struct mem_map_optimized* m, struct narrow_frame* frame, uint8_t n_threads, void* value, uint16_t valsz) {
    int n_rgns;
    int idx = 0;
    pthread_t** rgn_threads;

    // if we already have a narrowed frame
    if (frame->n_tracked) {
        renarrow_frame(frame, value, valsz);
        return;
    }

    // n_threads is really threads per region
    n_rgns = (_Bool)m->stack + (_Bool)m->heap + m->rgn.n_remaining + m->n_disk;
    rgn_threads = calloc(n_rgns, sizeof(pthread_t*));

    if (m->stack) {
        rgn_threads[0] = narrow_mmo_sub_spawner(frame, n_threads, m->stack, m->stack + rgn_len(&m->rgn.stack), value, valsz);
    }
    if (m->heap) {
        rgn_threads[1] = narrow_mmo_sub_spawner(frame, n_threads, m->heap, m->heap + rgn_len(&m->rgn.heap), value, valsz);
    }
    if (m->other) {
        for (int i = 0; i < m->rgn.n_remaining; ++i) {
            rgn_threads[2 + i] = narrow_mmo_sub_spawner(frame, n_threads, m->other[i], 
                                                        m->other[i] + rgn_len(&m->rgn.remaining_addr[i]), value, valsz);
        }
    }
    if (m->disk_mapped) {
        for (struct disk_map_inf* dm = m->disk_fns; dm; dm = dm ->next) {
        /*for (int i = 0; i < m->n_disk; ++i) {*/
            // TODO: fix range of addresses, need to store file size somewhere, potentially have disk_fns be a struct with name and size
            puts("narrowing on mapped_disk");
            rgn_threads[3 + m->rgn.n_remaining + idx++] = narrow_mmo_sub_spawner(frame, n_threads, dm->address, dm->address + dm->sz, value, valsz);
        }
    }

    for (int i = 0; i < n_rgns; ++i) {
        for (uint8_t j = 0; j < n_threads; ++j) {
            if (!rgn_threads[i]) {
                continue;
            }
            pthread_join(rgn_threads[i][j], NULL);
        }
    }
}

/* ~~~~~~~~~~~~~~~~end optimized feb 2025 changes~~~~~~~~~~~~~~~~~ */
