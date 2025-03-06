#include "vmem_access.h"

#include <string.h>
#include <assert.h>
#include <sys/uio.h>

// with less than RELOAD_CUTOFF values, it's faster to do individual reads for integers when updating mem_map
#define RELOAD_CUTOFF 1e5

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

BYTE* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e){
      int sz_rgn;
      if(vm_e == NULL)sz_rgn = bytes;
      else sz_rgn = (char*)vm_e-(char*)vm_s;
      BYTE* ret = malloc(sz_rgn);
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
      BYTE* bytes = (src_pid == getpid()) ? (BYTE*)src : read_bytes_from_pid_mem(src_pid, n_bytes, src, NULL);
      // don't use write_bytes_to_pid_mem to write to current process
      if(dest_pid == getpid())memcpy(dest, bytes, n_bytes);
      else ret = write_bytes_to_pid_mem(dest_pid, n_bytes, dest, bytes);
      if(src_pid != getpid())free(bytes);
      return ret;
}

bool write_bytes_to_pid_mem(pid_t pid, int bytes, void* vm, BYTE* value){
      struct iovec local;
      struct iovec remote;
      local.iov_base = value;
      local.iov_len = bytes;
      remote.iov_base = vm;
      remote.iov_len = bytes;
      return (bytes == process_vm_writev(pid, &local, 1, &remote, 1, 0));
}

bool write_int_to_pid_mem(pid_t pid, void* vm, int value){
      BYTE byte_int[4];
      memcpy(byte_int, &value, 4);
      return write_bytes_to_pid_mem(pid, 4, vm, byte_int);
}

bool write_str_to_pid_mem(pid_t pid, void* vm, const char* str){
      return write_bytes_to_pid_mem(pid, strlen(str), vm, (BYTE*)str);
}

/* ~~~~~~~~~~~~~~~~begin optimized feb 2025 changes~~~~~~~~~~~~~~~~~ */

void init_frames(struct mem_map_optimized* m) {
    m->n_frames = 0;
    m->frames = NULL;
    // this must be zeroed for NULL first frame
}

void init_mem_map_opt(struct mem_map_optimized* m) {
    m->stack = m->heap = NULL;
    m->other = NULL;

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
    if (m->stack) {
        free(m->stack);
    }
    if (m->heap) {
        free(m->heap);
    }

    if (m->other) {
        for (int i = 0; i < m->rgn.n_remaining; ++i) {
            free(m->other[i]);
        }
    }

    for (struct narrow_frame* f = m->frames; f; f = f->next) {
        free_frame(f);
    }
}

uint64_t rgn_len(struct m_addr_pair* addrs) {
    return ((uint8_t*)addrs->end - (uint8_t*)addrs->start);
}

// TODO: potentially only repopulate regions that have matches in frame
// TODO: this should fail when process shuts down
// TODO: detect failures from initial population
_Bool populate_mem_map_opt(struct mem_map_optimized* m, _Bool stack, _Bool heap, _Bool other) {
    // we'll assume caller sets m->rgn
    /*m->rgn = get_vmem_locations(pid, unmarked_additional);*/
    // using a byte value of 32 - hoping it'll be faster and shouldn't matter for actual data now that
    // we're lumping it all together
    int bytes = 32;
    int failures = stack + heap + m->rgn.n_remaining;
    if (stack) {
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
    if (heap) {
        if (m->heap) {
            if (read_bytes_from_pid_mem_dir(m->heap, m->rgn.pid, bytes, m->rgn.heap.start, m->rgn.heap.end)) {
                --failures;
            }
        } else {
            m->heap = read_bytes_from_pid_mem(m->rgn.pid, bytes, m->rgn.heap.start, m->rgn.heap.end);
            --failures;
        }
    }
    if (other) {
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

// TODO: write this code - should be a linked list. no need to be threadsafe, this will only be called
// from the main thread in varedit
void add_frame(struct mem_map_optimized* m, char* label) {
    struct narrow_frame* f = malloc(sizeof(struct narrow_frame));
    f->n_tracked = 0;
    strncpy(f->label, label, sizeof(f->label) - 1);
    f->next = m->frames;
    f->tracked_vars = NULL;
    f->current_type = NONE_T;
    m->frames = f;
    ++m->n_frames;
}

/*
 * could just insert replacing frame_var with a copy that has a prev value
*/
/*is this really threadsafe, seems not to be. */
void insert_frame_var(struct narrow_frame* frame, uint8_t* address, uint8_t len) {
    /*pthread_mutex_lock(&frame->lock);*/
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
    /*pthread_mutex_unlock(&frame->lock);*/
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

void rm_next_frame_var_unsafe(struct narrow_frame* frame, struct found_variable* v, _Bool rm_first) {
    struct found_variable* to_free;
    --frame->n_tracked;
    if (!v && rm_first) {
        to_free = frame->tracked_vars;
        frame->tracked_vars = frame->tracked_vars->next;
        // okay, this is causing segfault... WHY?
        /*free(to_free);*/
        return;
    }

    /*v == NULL at some point*/
    to_free = v->next;
    v->next = v->next->next;
    free(to_free);
}

// this version uses locks as a proof of concept and sets prev
/*first element must have next = NULL;*/
// must init frame to NULL!

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
    for (; first_byte_match; first_byte_match = memmem(i_rgn, end_rgn - i_rgn, value, valsz)) {
        if (first_byte_match >= end_rgn) {
            /*ALSO need to exit once first_byte_match is not in our defined region. could cause concurrency issues.*/
            return n_matches;
        }
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

        // TODO: i removed the check for i_rgn being between end_rgn, start_rgn - does this need
        // to be added back? I don't believe so because the above check should cover that
    }
    if (found) {
        printf("combining %li matches into frame\n", n_matches);
        combine_frame_var(frame, found, last, n_matches);
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
    for (struct found_variable* v = frame->tracked_vars; v && v->next; v = (removed) ? v : v->next) {
        // TODO: this may not hold true when it comes to strings
        //       for strings, scrap this assertion and memcmp with the smaller size
        assert(valsz == v->next->len);

        removed = 0;
        if (memcmp(v->next->address, value, valsz)) {
            rm_next_frame_var_unsafe(frame, v, 0);
            removed = 1;
        }
    }

    /* check first element */
    assert(valsz == frame->tracked_vars->len);
    if (frame->tracked_vars && memcmp(frame->tracked_vars->address, value, valsz)) {
        rm_next_frame_var_unsafe(frame, NULL, 1);
    }
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
        pthread_create(pth + i, NULL, narrow_mmo_sub_wrapper, targ);

        running_start = targ->end_rgn;
    }
    return pth;
}

/* narrows variables in memory of all  */
void narrow_mem_map_frame_opt(struct mem_map_optimized* m, struct narrow_frame* frame, uint8_t n_threads, void* value, uint16_t valsz) {
    int n_rgns;
    pthread_t** rgn_threads;

    // if we already have a narrowed frame
    if (frame->n_tracked) {
        renarrow_frame(frame, value, valsz);
        return;
    }

    // n_threads is really threads per region
    n_rgns = (_Bool)m->stack + (_Bool)m->heap + m->rgn.n_remaining;
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
