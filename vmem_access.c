#include "vmem_access.h"

#include <string.h>
#include <assert.h>
#include <sys/uio.h>

// with less than RELOAD_CUTOFF values, it's faster to do individual reads for integers when updating mem_map
#define RELOAD_CUTOFF 1e5

void free_blkstr(struct str_blk* blk){
      if(!blk->in_place)return;
      if(blk->stack)free(blk->stack);
      if(blk->heap)free(blk->heap);
      for(unsigned char i = 0; i < blk->n_ad; ++i)
            if(blk->addtnl[i])free(blk->addtnl[i]);
      if(blk->addtnl)free(blk->addtnl);
      free(blk);
}

void free_i_map(struct i_mmap_map* imm, int skip_index){
      for(int i = 0; i < imm->n_bux; ++i)
            if(i != skip_index)free(imm->i_buckets[i]);
      free(imm->i_buckets);
      free(imm->bucket_ref);
}

void free_i_blk(struct int_blk* ib){
      if(ib->stack)free(ib->stack);
      if(ib->heap)free(ib->heap);
      if(ib->addtnl){
            for(int i = 0; i < ib->n_ad; ++i){
                  free(ib->addtnl[i]);
            }
            free(ib->addtnl);
      }
}

void free_mem_map(struct mem_map* mem){
      if(mem->integers){
            /*if(mem->i_mmap_hash.in_place)free_i_map(&mem->i_mmap_hash, -1);*/
            /* we'll free i_map even if not in place because we know that it was once in place */
            /* if(mem->i_mmap_hash.in_place) */free_i_map(&mem->i_mmap_hash, -1);
            /*free(mem->i_mmap);*/
            free_i_blk(mem->i_blk);
            free(mem->i_blk);
            return;
      }
      if(!mem->blk->in_place){
            for(unsigned int i = 0; i < mem->size; ++i)
                  free(mem->s_mmap[i].value);
      }
      else free_blkstr(mem->blk);
      free(mem->s_mmap); 
      mem->s_mmap = NULL;
}

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

void resize_str_mmap(struct mem_map* c_mm, unsigned int* m_size, int factor){
      *m_size *= factor;
      struct addr_str_pair* tmp = malloc(sizeof(struct addr_str_pair)*(*m_size));
      memcpy(tmp, c_mm->s_mmap, sizeof(struct addr_str_pair)*c_mm->size);
      free(c_mm->s_mmap);
      c_mm->s_mmap = tmp;
}

// if mem == NULL, a pointer to a malloc'd mem_map struct is returned
struct mem_map* mem_map_init(struct mem_map* mem, pid_t pid, bool unmarked_additional){
      if(!mem)mem = malloc(sizeof(struct mem_map));
      mem->size = mem->i_size = mem->s_size = 0;
      mem->mapped_rgn = get_vmem_locations(pid, unmarked_additional);
      mem->low_mem = false;
      mem->force_block_str = true;
      // to silence valgrind errors, overwritten by populate_mem_map's set_mode_mem_map call
      mem->integers = true;
      return mem;
}

// stores mem->i_size or mem->s_size for later use, sets up mem->size
bool set_mode_mem_map(struct mem_map* mem, bool integers){
      if(mem->integers == integers)return false;
      if(mem->integers)mem->i_size = mem->size;
      else mem->s_size = mem->size;
      mem->integers = integers;
      mem->size = integers ? mem->i_size : mem->s_size;
      return true;
}

void init_i_map(struct i_mmap_map* imm, int n_bux, int n_entries){
      imm->n_bux = n_bux;
      imm->i_buckets = calloc(n_bux, sizeof(struct addr_int_pair*));
      imm->bucket_ref = calloc(n_bux, sizeof(int));

      /* to limit the amount of memory used, we'll assume that most buckets will have less than n_entries/n_bux */
      /* this, of course, implies that we'll need to resize buckets in some cases */
      int bucket_sz = 0.7*(n_entries/n_bux);

      for(int i = 0; i < n_bux; ++i){
            imm->i_buckets[i] = calloc((bucket_sz+1), sizeof(struct addr_int_pair));
            imm->i_buckets[i][bucket_sz].addr = (void*)0xdecaf;
      }

      imm->in_place = 1;
}

void insert_i_map(struct i_mmap_map* imm, void* addr, int* value){
      int ind = *value % imm->n_bux;
      ind = (ind < 0) ? -1*ind : ind;
      int ind_prog = imm->bucket_ref[ind];
      if(imm->i_buckets[ind][ind_prog].addr == (void*)0xdecaf){
            struct addr_int_pair* tmp_ip = malloc(sizeof(struct addr_int_pair)*((ind_prog*2)+1));
            tmp_ip[(ind_prog*2)].addr = (void*)0xdecaf;
            memcpy(tmp_ip, imm->i_buckets[ind], sizeof(struct addr_int_pair)*ind_prog);
            free(imm->i_buckets[ind]);
            imm->i_buckets[ind] = tmp_ip;
      }
      imm->i_buckets[ind][ind_prog].addr = addr;
      imm->i_buckets[ind][imm->bucket_ref[ind]++].value = value;
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
        /*puts("FAIled");*/
    }
    /*printf("succesfully inserted a new frame var");*/
    /*pthread_mutex_unlock(&frame->lock);*/
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
uint64_t narrow_mem_map_frame_opt_subroutine(struct narrow_frame* frame, uint8_t* start_rgn, uint8_t* end_rgn, void* value, uint16_t valsz) {
    uint64_t n_matches = 0;
    uint8_t* first_byte_match = start_rgn;
    uint8_t* i_rgn = start_rgn;
    // this is only necessary on the first pass - we can know this by n_tracked!
    // until we exhaust all matches of first byte
    for (; first_byte_match && i_rgn < end_rgn; first_byte_match = memmem(i_rgn, end_rgn - i_rgn, value, valsz)) {
        if (first_byte_match >= end_rgn) {
            /*ALSO need to exit once first_byte_match is not in our defined region. could cause concurrency issues.*/
            return n_matches;
        }
        ++n_matches;
        /*puts("found a match!");*/
        insert_frame_var(frame, first_byte_match, valsz);
        i_rgn = first_byte_match + valsz;
        // thea bove could be causing the problem by incrementing i_rgn past valid memory
        /*OH! check i_rgn + valsz?*/
        // does this invalidate assumptions for boundary variables?
        if (!(i_rgn < end_rgn && i_rgn > start_rgn)) {
            puts("INVALID");
            return n_matches;
        }
        #if 0
        if (!memcmp(first_byte_match, value, valsz)) {
            ++n_matches;
            /*puts("found a match!");*/
            insert_frame_var(frame, first_byte_match, valsz);
            if (valsz != 4) {
                /*printf("inserting valsz of %i\n", valsz);*/
            }
            /*insert_frame_var_lock(frame, first_byte_match, valsz);*/
            i_rgn = first_byte_match + valsz;
        } else {
            /*puts("incrementing i_rgn");*/
            i_rgn = first_byte_match + 1;
        }
        #endif
        /*
         * printf("TRYING MCH for memchr(%p, %i, %li)\n", i_rgn, *((uint8_t*)value), end_rgn - i_rgn);
         * printf("mchr: %p\n", memchr(i_rgn, *((uint8_t*)value), end_rgn - i_rgn));
        */
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
    /*printf("addr_per_th: %li, should be a multiple of: %i\n", addr_per_th, valsz);*/

    /*printf("start region: %p, end: %p\n", start, end);*/
    /*oh shit, this needs to be in increments of valsz!*/
    // this also could cause problems on boundaries of ranges - we can't memchr() for first byte match
    // if it's on a boundary! OH! we can actually mark down if we found a first byte match at the end of a
    // range!
    // this would also make it so we don't need ranges of multiples of valsz

    /* it is not necessary for addr_per_th to be a multiple of valsz
     * if the first byte of a match is found by a narrow thread and the rest of that match
     * lies on the other side of a boundary, the thread will still be okay to insert that value into
     * the frame. this is perfectly safe, as no other thread will insert this duplicate
     * since its first byte is behind their region's start. also, no writes are made to the buffer during
     * initial narrowing.
     */
    for (uint8_t i = 0; i < n_threads; ++i) {
    /*iterate, increment start_rgn and end_rgn by same amount each iteration to ensure that it ends up on last address!*/
        /* in case of truncation during division, make sure to reach the end of memory region in last thread */
        /*arg.end_rgn = arg.start_rgn + addr_per_th;*/
        targ = malloc(sizeof(struct narrow_mmo_sub_args));
        targ->frame = frame;
        targ->start_rgn = running_start;
        targ->value = value;
        targ->valsz = valsz;
        targ->end_rgn = targ->start_rgn + addr_per_th;

        if (i == n_threads - 1) {
            targ->end_rgn = end;
        }
        printf("spawned thread %i with %p -> %p\n", i, targ->start_rgn, targ->end_rgn);
        /*pthread_create(pth + i, NULL, narrow_mmo_sub_wrapper, &arg);*/
        pthread_create(pth + i, NULL, narrow_mmo_sub_wrapper, targ);
        /*pthread_join(pth[i], NULL);*/

        running_start = targ->end_rgn;
    }
    return pth;
}

/* narrows variables in memory of all  */
// TODO: get multithreading working with this function
void narrow_mem_map_frame_opt(struct mem_map_optimized* m, struct narrow_frame* frame, uint8_t n_threads, void* value, uint16_t valsz) {
    int n_rgns;
    pthread_t** rgn_threads;

    // if we already have a narrowed frame
    if (frame->n_tracked) {
        renarrow_frame(frame, value, valsz);
        return;
    }

    // TODO: implement multithreaded narrowing!
    /*assert(n_threads == 1);*/
    // n_threads is really threads per region
    n_rgns = (_Bool)m->stack + (_Bool)m->heap + m->rgn.n_remaining;
    rgn_threads = calloc(n_rgns, sizeof(pthread_t*));

    if (m->stack) {
        puts("STACK");
        rgn_threads[0] = narrow_mmo_sub_spawner(frame, n_threads, m->stack, m->stack + rgn_len(&m->rgn.stack), value, valsz);
    }
    if (m->heap) {
        puts("HEAP");
        rgn_threads[1] = narrow_mmo_sub_spawner(frame, n_threads, m->heap, m->heap + rgn_len(&m->rgn.heap), value, valsz);
    }
    if (m->other) {
        puts("OTHER");
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
            printf("joined thread %i:%i\n", i, j);
        }
    }
}

/* ~~~~~~~~~~~~~~~~end optimized feb 2025 changes~~~~~~~~~~~~~~~~~ */

void populate_mem_map(struct mem_map* mem, int d_rgn, bool use_additional_rgns, bool integers, int bytes){
      // store size, toggle mem->integers in case mem is being repopulated with a different mode
      set_mode_mem_map(mem, integers);
      mem->size = 0;
      mem->int_mode_bytes = bytes;
      mem->d_rgn = d_rgn;
      mem->use_addtnl = use_additional_rgns;
      unsigned int m_size = 0;
      if(d_rgn == STACK || d_rgn == BOTH)m_size = (char*)mem->mapped_rgn.stack.end-(char*)mem->mapped_rgn.stack.start;
      if(d_rgn == HEAP || d_rgn == BOTH)m_size += (char*)mem->mapped_rgn.heap.end-(char*)mem->mapped_rgn.heap.start;
      if(use_additional_rgns)
            for(int i = 0; i < mem->mapped_rgn.n_remaining; ++i)
                  m_size += (char*)mem->mapped_rgn.remaining_addr[i].end-(char*)mem->mapped_rgn.remaining_addr[i].start;
      unsigned long buf_s = 0;
      // TODO: fix integer mode when int_mode_bytes > 4
      if(integers){
            /* populate_mem_map will always return an int mmap with hashed integers */
            /* integer mmaps will always point to an int_blk */
            /* TODO: free sections of int_blk when possible */
            m_size /= bytes;

            init_i_map(&mem->i_mmap_hash, 50, m_size);

            mem->i_blk = malloc(sizeof(struct int_blk));
            mem->i_blk->stack = mem->i_blk->heap = NULL;
            mem->i_blk->addtnl = NULL;
            mem->i_blk->n_ad = 0;

            mem->i_mmap = malloc(sizeof(struct addr_int_pair)*m_size);

            if(d_rgn == STACK || d_rgn == BOTH){
                  BYTE* ints_in_stack = read_bytes_from_pid_mem(mem->mapped_rgn.pid, bytes, mem->mapped_rgn.stack.start, mem->mapped_rgn.stack.end);
                  mem->i_blk->stack = ints_in_stack;
                  // TODO: in place math optimization
                  /* this iteration is very slow
                   * population can be sped up if only an initial mem addr is stored and ((char*)m_addr)+index_chk*bytes is computed on the fly
                   * easy to reason about, we need to find m_addr of value at index index_chk, each value takes up n_bytes
                   *
                   * this will decrease both space and time complexity significantly - we no longer need to iterate over each n_bytes of of mem
                   * and we no longer need to store sizeof(void*) for every single addr_{int,str}_pair
                   */
                  for(char* sp = mem->mapped_rgn.stack.start; sp != mem->mapped_rgn.stack.end; sp += bytes){
                        insert_i_map(&mem->i_mmap_hash, (void*)sp, (int*)(ints_in_stack+buf_s));
                        buf_s += bytes;
                        ++mem->size;
                  }
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  buf_s = 0;
                  BYTE* ints_in_heap = read_bytes_from_pid_mem(mem->mapped_rgn.pid, bytes, mem->mapped_rgn.heap.start, mem->mapped_rgn.heap.end);
                  mem->i_blk->heap = ints_in_heap;
                  for(char* hp = mem->mapped_rgn.heap.start; hp != mem->mapped_rgn.heap.end; hp += bytes){
                        insert_i_map(&mem->i_mmap_hash, (void*)hp, (int*)(ints_in_heap+buf_s));
                        buf_s += bytes;
                        ++mem->size;
                  }
            }
            if(use_additional_rgns){
                  mem->i_blk->n_ad = mem->mapped_rgn.n_remaining;
                  mem->i_blk->addtnl = malloc(sizeof(BYTE*)*mem->i_blk->n_ad);
                  for(int i = 0; i < mem->mapped_rgn.n_remaining; ++i){
                        BYTE* ints_in_addtnl = read_bytes_from_pid_mem(mem->mapped_rgn.pid, bytes, mem->mapped_rgn.remaining_addr[i].start,
                                                                                   mem->mapped_rgn.remaining_addr[i].end);
                        mem->i_blk->addtnl[i] = ints_in_addtnl;
                        buf_s = 0;
                        for(char* ap = mem->mapped_rgn.remaining_addr[i].start;
                                  ap != mem->mapped_rgn.remaining_addr[i].end; ap += bytes){

                              insert_i_map(&mem->i_mmap_hash, (void*)ap, (int*)(ints_in_addtnl+buf_s));
                              buf_s += bytes;
                              ++mem->size;
                        }
                  }
            }
      }
      else{ // !integers
            m_size /= 10;
            mem->s_mmap = malloc(sizeof(struct addr_str_pair)*m_size);
            /* populate_mem_map will always result in in_place strs
             * even in case of low_mem, strings are malloc'd in narrow_mem_map_str */
            // these must be initialized to NULL to avoid free errors in free_mem_map
            mem->blk = malloc(sizeof(struct str_blk));
            mem->blk->stack = mem->blk->heap = NULL;
            mem->blk->addtnl = NULL;
            mem->blk->in_place = true;
            mem->blk->n_ad = 0;
            int len;
            if(d_rgn == STACK || d_rgn == BOTH){
                  char* chars_in_stack = (char*)read_bytes_from_pid_mem(mem->mapped_rgn.pid, 1, mem->mapped_rgn.stack.start, mem->mapped_rgn.stack.end);
                  mem->blk->stack = chars_in_stack;
                  int n_addr = (char*)mem->mapped_rgn.stack.end-(char*)mem->mapped_rgn.stack.start;
                  for(char* cis_p = chars_in_stack; cis_p != chars_in_stack+n_addr; ++cis_p){
                        if(*cis_p > 0 && *cis_p < 127){
                              // resize by a factor of 2 - this is repeated in heap and additional region sections
                              if(mem->size == m_size)resize_str_mmap(mem, &m_size, 2);
                              len = strlen(cis_p);
                              mem->s_mmap[mem->size].addr = (void*)((char*)mem->mapped_rgn.stack.start+(cis_p-chars_in_stack));
                              mem->s_mmap[mem->size++].value = cis_p;
                              cis_p += len;
                        }
                  }
            }
            if(d_rgn == HEAP || d_rgn == BOTH){
                  char* chars_in_heap = (char*)read_bytes_from_pid_mem(mem->mapped_rgn.pid, 1, mem->mapped_rgn.heap.start, mem->mapped_rgn.heap.end);
                  mem->blk->heap = chars_in_heap;
                  int n_addr = (char*)mem->mapped_rgn.heap.end-(char*)mem->mapped_rgn.heap.start;
                  len = 0;
                  for(char* cih_p = chars_in_heap; cih_p != chars_in_heap+n_addr; ++cih_p){
                        if(*cih_p > 0 && *cih_p < 127){
                              if(mem->size == m_size)resize_str_mmap(mem, &m_size, 2);
                              len = strlen(cih_p);
                              mem->s_mmap[mem->size].addr = (void*)((char*)mem->mapped_rgn.heap.start+(cih_p-chars_in_heap));
                              mem->s_mmap[mem->size++].value = cih_p;
                              cih_p += len;
                        }
                  }
            }
            if(use_additional_rgns){
                  int n_addr;
                  char* chars_in_addtnl;
                  mem->blk->n_ad = mem->mapped_rgn.n_remaining;
                  mem->blk->addtnl = malloc(sizeof(char*)*mem->blk->n_ad);
                  len = 0;
                  for(int i = 0; i < mem->mapped_rgn.n_remaining; ++i){
                        chars_in_addtnl = (char*)read_bytes_from_pid_mem(mem->mapped_rgn.pid, 1, mem->mapped_rgn.remaining_addr[i].start, mem->mapped_rgn.remaining_addr[i].end);
                        mem->blk->addtnl[i] = chars_in_addtnl;
                        n_addr = (char*)mem->mapped_rgn.remaining_addr[i].end-(char*)mem->mapped_rgn.remaining_addr[i].start;
                        for(char* cia_p = chars_in_addtnl; cia_p != chars_in_addtnl+n_addr; ++cia_p){
                              if(*cia_p > 0 && *cia_p < 127){
                                    if(mem->size == m_size)resize_str_mmap(mem, &m_size, 2);
                                    len = strlen(cia_p);
                                    mem->s_mmap[mem->size].addr = (void*)((char*)mem->mapped_rgn.remaining_addr[i].start+(cia_p-chars_in_addtnl));
                                    mem->s_mmap[mem->size++].value = cia_p;
                                    cia_p += len;
                              }
                        }
                  }
            }
            if(mem->size < m_size){
                  struct addr_str_pair* tmp_s_mmap = malloc(sizeof(struct addr_str_pair)*mem->size);
                  memcpy(tmp_s_mmap, mem->s_mmap, sizeof(struct addr_str_pair)*mem->size);
                  free(mem->s_mmap);
                  mem->s_mmap = tmp_s_mmap;
            }
      }
}

/* TODO: combine flatten_i_mmap_hash() and regularize_i_mmap_hash() */
void flatten_i_mmap_hash(struct mem_map* mem){
      struct addr_int_pair* tmp_aip = malloc(sizeof(struct addr_int_pair)*(mem->size+1));
      tmp_aip[mem->size].addr = (void*)0xdecaf;
      int ind = 0;
      for(int i = 0; i < mem->i_mmap_hash.n_bux; ++i){
            for(int j = 0; j < mem->i_mmap_hash.bucket_ref[i]; ++j){
                  tmp_aip[ind++] = mem->i_mmap_hash.i_buckets[i][j];
            }
            /* TODO: remove this (?)  */
            free(mem->i_mmap_hash.i_buckets[i]);
      }
      mem->i_mmap_hash.n_bux = 1;
      mem->i_mmap_hash.bucket_ref[0] = mem->size;
      mem->i_mmap_hash.i_buckets[0] = tmp_aip;
}

/* regularize_i_mmap_hash() converts a one dimensional i_mmap_hash that has been narrowed
 * into a standar i_mmap
 */
_Bool regularize_i_mmap_hash(struct mem_map* mem){
      /*this should only be called when there is just one bucket left*/
      if(!mem->i_mmap_hash.in_place || mem->i_mmap_hash.n_bux != 1)return 0;
      free(mem->i_mmap);

      mem->i_mmap = *mem->i_mmap_hash.i_buckets;
      mem->i_size = mem->size = *mem->i_mmap_hash.bucket_ref;

      mem->i_mmap_hash.in_place = 0;
      return 1;
}

void update_mem_map(struct mem_map* mem){
      if(mem->size == 0)return;
      if(mem->integers){
            /* TODO: should this always be used?
             * it is much simpler as it will work with either i_mmap_hash or i_mmap
             * and is often faster
             */
            if(!mem->low_mem && mem->size > 100){

                  if(mem->i_blk->stack)
                        read_bytes_from_pid_mem_dir(mem->i_blk->stack, mem->mapped_rgn.pid, mem->int_mode_bytes,
                                                    mem->mapped_rgn.stack.start, mem->mapped_rgn.stack.end);
                  if(mem->i_blk->heap)
                        read_bytes_from_pid_mem_dir(mem->i_blk->heap, mem->mapped_rgn.pid, mem->int_mode_bytes,
                                                    mem->mapped_rgn.heap.start, mem->mapped_rgn.heap.end);
                  for(unsigned char i = 0; i < mem->i_blk->n_ad; ++i){
                        if(mem->i_blk->addtnl[i])
                        read_bytes_from_pid_mem_dir(mem->i_blk->addtnl[i], mem->mapped_rgn.pid, mem->int_mode_bytes,
                                                    mem->mapped_rgn.remaining_addr[i].start, mem->mapped_rgn.remaining_addr[i].end);
                  }
                  return;
            }

            /* if we have few enough values it will be worthwhile to flatten and regularize */
            if(mem->i_mmap_hash.n_bux != 1)flatten_i_mmap_hash(mem);
            regularize_i_mmap_hash(mem);
            for(unsigned int i = 0; i < mem->size; ++i)
                  *mem->i_mmap[i].value = read_single_val_from_pid_mem(mem->mapped_rgn.pid, mem->int_mode_bytes, mem->i_mmap[i].addr);

            return;
      }
      // TODO: should string update optimization always be used? it's much faster
      /* TODO: should this always be used for integers with new hash storage population that takes much longer? */
      if(mem->low_mem || (!mem->blk->in_place || mem->size < RELOAD_CUTOFF/10000)){
            for(unsigned int i = 0; i < mem->size; ++i)
                  // this method works for both blkstr mode and individually alloc'd strings
                  read_bytes_from_pid_mem_dir(mem->s_mmap[i].value, mem->mapped_rgn.pid, strlen(mem->s_mmap[i].value), mem->s_mmap[i].addr, NULL);
      }
      else{ // faster but more memory intensive update methods
            if(mem->blk->stack)
                  read_bytes_from_pid_mem_dir(mem->blk->stack, mem->mapped_rgn.pid, 1, mem->mapped_rgn.stack.start, mem->mapped_rgn.stack.end);
            if(mem->blk->heap)
                  read_bytes_from_pid_mem_dir(mem->blk->heap, mem->mapped_rgn.pid, 1, mem->mapped_rgn.heap.start, mem->mapped_rgn.heap.end);
            for(unsigned char i = 0; i < mem->blk->n_ad; ++i){
                  if(mem->blk->addtnl[i])
                  read_bytes_from_pid_mem_dir(mem->blk->addtnl[i], mem->mapped_rgn.pid, 1, mem->mapped_rgn.remaining_addr[i].start, mem->mapped_rgn.remaining_addr[i].end);
            }
      }
}

void narrow_mem_map_int_nopt(struct mem_map* mem, int match){
      unsigned int initial = mem->size;
      for(unsigned int i = 0; i < mem->size; ++i){
            if(*mem->i_mmap[i].value != match){
                  mem->i_mmap[i--] = mem->i_mmap[--mem->size];
                  /*
                   *  // more simply,
                   *  mmap[i] = mmap[mem->size-1];
                   *  --mem->size;
                   *  --i;
                   *
                   */
            }
      }
      if(mem->size == 0){
            free_mem_map(mem);
            return;
      }
      if(mem->size < initial){
            struct addr_int_pair* tmp_mmap = malloc(sizeof(struct addr_int_pair)*mem->size);
            memcpy(tmp_mmap, mem->i_mmap, sizeof(struct addr_int_pair)*mem->size);
            free(mem->i_mmap);
            mem->i_mmap = tmp_mmap;
      }
}

void narrow_mem_map_int(struct mem_map* mem, int match){
      if(!mem->i_mmap_hash.in_place)narrow_mem_map_int_nopt(mem, match);
      else{
            int ind = match % mem->i_mmap_hash.n_bux;
            ind = (ind < 0) ? -1*ind : ind;

            for(int i = 0; i < mem->i_mmap_hash.n_bux; ++i){
                  if(i != ind){
                        free(mem->i_mmap_hash.i_buckets[i]);
                        /* TODO: should we just hide values instead of freeing them? */
                        mem->i_mmap_hash.bucket_ref[i] = 0;
                  }
            }

            struct addr_int_pair** tmp_aip = malloc(sizeof(struct addr_int_pair*));
            tmp_aip[0] = malloc(sizeof(struct addr_int_pair)*(mem->i_mmap_hash.bucket_ref[ind]+1));
            tmp_aip[0][mem->i_mmap_hash.bucket_ref[ind]].addr = (void*)0xdecaf;

            /* TODO:
             * we could ignore the problem of finding matches and just regularize
             * the matche can be found using narrow_mem_map_int_nopt()
             * just simply:
             *       throw out buckets that we don't need
             *       keep one
             *       regularize
             *       _nopt()
            */


            unsigned int adj_size = 0;
            for(int i = 0; i < mem->i_mmap_hash.bucket_ref[ind]; ++i)
                  if(*mem->i_mmap_hash.i_buckets[ind][i].value == match){
                        tmp_aip[0][adj_size].value = mem->i_mmap_hash.i_buckets[ind][i].value;
                        tmp_aip[0][adj_size++].addr = mem->i_mmap_hash.i_buckets[ind][i].addr;
                  }
            

            /* TODO: this should leverage init* */
            mem->i_mmap_hash.n_bux = 1;
            free(mem->i_mmap_hash.bucket_ref);
            mem->i_mmap_hash.bucket_ref = malloc(sizeof(int));
            *mem->i_mmap_hash.bucket_ref = adj_size;

            free(mem->i_mmap_hash.i_buckets[ind]);
            free(mem->i_mmap_hash.i_buckets);
            mem->i_mmap_hash.i_buckets = tmp_aip;

            regularize_i_mmap_hash(mem);
      }
}

/* this function finds the last match of needle in haystack, useful for exact_e in narrow_mem_map_str
 * TODO: is there a better way to do this?
 */
char* adj_m(char* haystack, const char* needle, bool exact_s){
      if(exact_s)return haystack;
      char* c = haystack;
      char* p;
      while((p = strstr(c, needle)))c = ++p;
      return c-1;
}

void narrow_mem_map_str(struct mem_map* mem, const char* match, bool exact_s, bool exact_e){
      // if match string is a malformed string
      if(!match || !*match)return;
      unsigned int initial = mem->size;
      int mlen = strlen(match);
      for(unsigned int i = 0; i < mem->size; ++i){
            char* s = strstr(mem->s_mmap[i].value, match);
            if(!s || (exact_s && s != mem->s_mmap[i].value) || (exact_e && adj_m(mem->s_mmap[i].value, match, exact_s)[mlen] != '\0')){
                  if(!mem->blk->in_place)free(mem->s_mmap[i].value);
                  --mem->size;
                  if(mem->size == 0)break;
                  mem->s_mmap[i--] = mem->s_mmap[mem->size];
            }
      }
      // to make sure not to try to reallocate empty mmap with resize
      if(mem->size == 0){
            free_mem_map(mem);
            return;
      }
      if(mem->size < initial){
            struct addr_str_pair* tmp_s_mmap = malloc(sizeof(struct addr_str_pair)*mem->size);
            /* if we have low memory, or have narrowed sufficiently, it's worthwhile to indiviually allocate strings
             * this is trivial because we are realloc'ing anyway for a resize and need to copy entries */
            // TODO: test usage of RELOAD_CUTOFF for this condition 
            if(mem->blk->in_place && (mem->low_mem || mem->size < (initial/1000) || mem->size < 1000)){
                  // if we have very few values, it's likely that they are not from a diverse group of regions
                  // we'll try to free any unused region blocks
                  if(mem->force_block_str){
                        int rgn;
                        bool s = false, h = false;
                        // +1 in case of n_ad == 0
                        bool a[mem->blk->n_ad+1];
                        memset(a, 0, mem->mapped_rgn.n_remaining);
                        for(unsigned int i = 0; i < mem->size; ++i){
                              which_rgn(mem->mapped_rgn, mem->s_mmap[i].addr, &rgn);
                              switch(rgn){
                                    case STACK: s = true; break;
                                    case HEAP:  h = true; break;
                                    default:    if(rgn >= 2)a[rgn-2] = true;
                              }
                              tmp_s_mmap[i] = mem->s_mmap[i];
                        }
                        if(!s && mem->blk->stack){
                              free(mem->blk->stack); mem->blk->stack = NULL;
                        }
                        if(!h && mem->blk->heap){
                              free(mem->blk->heap); mem->blk->heap = NULL;
                        }
                        for(unsigned char i = 0; i < mem->blk->n_ad; ++i){
                              if(!a[i] && mem->blk->addtnl[i]){
                                    free(mem->blk->addtnl[i]); mem->blk->addtnl[i] = NULL;
                              }
                        }
                  }
                  else{
                        for(unsigned int i = 0; i < mem->size; ++i){
                              tmp_s_mmap[i].addr = mem->s_mmap[i].addr;
                              tmp_s_mmap[i].value = strdup(mem->s_mmap[i].value);
                        }
                        free_mem_map(mem);
                        mem->blk->in_place = false;
                  }
            }
            else
                  memcpy(tmp_s_mmap, mem->s_mmap, sizeof(struct addr_str_pair)*mem->size);
            if(mem->s_mmap)free(mem->s_mmap);
            mem->s_mmap = tmp_s_mmap;
      }
}

// TODO: possibly move all lock functions and struct definitions to separate files mem_lock.{c,h}
bool print_locks(struct lock_container* lc){
      if(lc->n == lc->n_removed)return false;
      unsigned int r_i = 0;
      for(unsigned int i = 0; i < lc->n; ++i){
            if(!lc->locks[i].active)continue;
            // strings
            if(!lc->locks[i].integers)printf("(%i) %p: \"%s\"", r_i, *lc->locks[i].m_addr, *lc->locks[i].s_val);
            else printf("(%i) %p: %i", r_i, *lc->locks[i].m_addr, *lc->locks[i].i_val);
            if(lc->locks[i].rng)fputs(" (multiple locks)", stdout); 
            puts("");
            ++r_i;
      }
      return true;
}

/* if keep_first, the s_val in the rm_s of lc will not be freed
   every other string in s_val that requires freeing will still be freed
   M_ADDR, I_VAL AND S_VAL WILL NOT BE FREED BY REMOVE_LOCK UNLESS FREE_OP IS SET as follows: 0: nothing, 1: just maddr, 2: just i/sval, 3: m and i/s */
   // TODO: add free_op documentation
// remove_lock returns long as to not truncate unsigned int - must be signed to return -1 when lock not found
long remove_lock(struct lock_container* lc, unsigned int rm_s, bool keep_first, char free_op){
      if(lc->n == lc->n_removed || rm_s >= lc->n-lc->n_removed)return -1;
      unsigned int r_i = 0;
      pthread_mutex_lock(&lc->mut_lck);
      long ret = -1;
      for(unsigned int i = 0; i < lc->n; ++i){
            if(!lc->locks[i].active)continue;
            if(r_i == rm_s){
                  ++lc->n_removed;
                  lc->locks[i].active = false;
                  if(!lc->locks[i].integers){
                        unsigned int u_lim = (lc->locks[i].mul_val) ? lc->locks[i].n_addr : 1;
                        for(unsigned int f = keep_first; f < u_lim; ++f)free(lc->locks[i].s_val[f]);
                  }
                  ret = i;
                  break;
            }
            ++r_i;
      }
      pthread_mutex_unlock(&lc->mut_lck);
      if(lc->n == lc->n_removed)pthread_join(lc->thread, NULL);
      if(free_op%2 != 0)free(lc->locks[ret].m_addr);
      if(free_op > 1){
            if(lc->locks[ret].integers)free(lc->locks[ret].i_val);
            else if(!keep_first)free(lc->locks[ret].s_val);
      }
      return ret;
}

// returns number of locks removed before freeing
unsigned int free_locks(struct lock_container* lc, char free_op){
      unsigned int i = 0;
      for(i = 0; remove_lock(lc, 0, false, free_op) != -1; ++i);
      free(lc->locks);
      pthread_mutex_destroy(&lc->mut_lck);
      return i;
}

// if lc == NULL, a pointer to a malloc'd lock_container struct is returned
struct lock_container* lock_container_init(struct lock_container* lc, unsigned int initial_sz){
      if(lc == NULL)lc = malloc(sizeof(struct lock_container));
      lc->n = lc->n_removed = 0;
      lc->cap = initial_sz;
      lc->locks = malloc(sizeof(struct lock_entry)*lc->cap);
      pthread_mutex_t mlck;
      pthread_mutex_init(&mlck, NULL);
      lc->mut_lck = mlck;
      return lc;
}

unsigned long lock_th(struct lock_container* lc){
      unsigned long iter = 0;
      while(lc->n != lc->n_removed){
            ++iter;
            usleep(1000);
            pthread_mutex_lock(&lc->mut_lck);
            for(unsigned int i = 0; i < lc->n; ++i){
                  if(lc->locks[i].active){
                        for(unsigned int j = 0; j < lc->locks[i].n_addr; ++j){
                              if(lc->locks[i].integers){
                                    if(lc->locks[i].mul_val)write_int_to_pid_mem(lc->locks[i].pid, lc->locks[i].m_addr[j], lc->locks[i].i_val[j]);
                                    else write_int_to_pid_mem(lc->locks[i].pid, lc->locks[i].m_addr[j], *lc->locks[i].i_val);
                              }
                              else{
                                    if(lc->locks[i].mul_val)write_str_to_pid_mem(lc->locks[i].pid, lc->locks[i].m_addr[j], lc->locks[i].s_val[j]);
                                    else write_str_to_pid_mem(lc->locks[i].pid, lc->locks[i].m_addr[j], *lc->locks[i].s_val);
                              }
                        }
                  }
            }
            pthread_mutex_unlock(&lc->mut_lck);
      }
      return iter;
}

void* lock_pthread(void* lc){
      return (void*)lock_th((struct lock_container*)lc);
}

// TODO add int_mode_bytes functionality
// returns true if thread was created, false if thread not created
bool create_lock(struct lock_container* lc, pid_t pid, void** addr, int* i_val, char** s_val, unsigned int n_addr, bool mul_val, bool integers){
      pthread_t lock_th;
      pthread_mutex_lock(&lc->mut_lck);
      if(lc->n == lc->cap){
            lc->cap *= 2;
            struct lock_entry* tmp_l = malloc(sizeof(struct lock_entry)*lc->cap);
            memcpy(tmp_l, lc->locks, sizeof(struct lock_entry)*lc->n);
            free(lc->locks);
            lc->locks = tmp_l;
      }
      if(integers)lc->locks[lc->n].s_val = NULL;
      lc->locks[lc->n].active = true;
      lc->locks[lc->n].pid = pid;
      lc->locks[lc->n].integers = integers;
      lc->locks[lc->n].mul_val = mul_val;
      lc->locks[lc->n].n_addr = n_addr;
      lc->locks[lc->n].s_val = s_val;
      lc->locks[lc->n].i_val = i_val;
      lc->locks[lc->n].rng = n_addr != 1;
      lc->locks[lc->n].m_addr = addr;
      ++lc->n;
      pthread_mutex_unlock(&lc->mut_lck);
      // if we have one lock after adding one - if we just added the first lock
      if(lc->n-1 == lc->n_removed){
            pthread_create(&lock_th, NULL, &lock_pthread, lc);
            lc->thread = lock_th;
            return true;
      }
      return false;
}
