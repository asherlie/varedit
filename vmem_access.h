#include "vmem_parser.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#define MEMCARVE_VER "libmemcarve 1.8.6"

typedef unsigned char BYTE;

/* ~~~~~~~~~~~~~~~~begin optimized feb 2025 changes~~~~~~~~~~~~~~~~~ */

struct found_variable{
    uint8_t* address;
    uint8_t len;

    struct found_variable* next;
};

/*
 * there are benefits and drawbacks to using a LL - it's nice because narrowing is easier. can just redirect pointers
 * and free up memory, as opposed to keeping memory allocated
 *
 * array would occupy less total memory at its peak, however
 * try LL for now.
 */

enum type_found { NONE_T, STRING, INT, LONG, FLOAT, DOUBLE };
// TODO: make this a lock free linked list
struct narrow_frame{
    char label[16];
    // TODO: are these parens right?
    // TODO: tracked variables must be abled to get linked back to the vmem of external process
    //_Atomic (struct found_variable*) tracked_vars;
    struct found_variable* _Atomic tracked_vars;
    _Atomic int n_tracked;
    /* type can change over time with new searches or writes - this only records
     * type from the most recent operation
     */
    enum type_found current_type;

    struct narrow_frame* next;
};

// we'll be using named frames to keep track of different collections of tracked variables
// this way we can effortlessly switch between them
// to keep track of a variable we need a pointer into a region and size of that variable
struct mem_map_optimized{
    enum m_region selected_rgns;
    struct mem_rgn rgn;
    uint8_t* heap;
    uint8_t* stack;
    uint8_t** other;
    // use rgn data for this
    //uint8_t n_other;

    struct narrow_frame* frames;
    int n_frames;
};

_Bool populate_mem_map_opt(struct mem_map_optimized* m);
void insert_frame_var(struct narrow_frame* frame, uint8_t* address, uint8_t len);
void narrow_mem_map_frame_opt(struct mem_map_optimized* m, struct narrow_frame* frame, uint8_t n_threads, void* value, uint16_t valsz);
void init_mem_map_opt(struct mem_map_optimized* m, enum m_region rgn);
void add_frame(struct mem_map_optimized* m, char* label);

//void p_frame_var(struct mem_map_optimized* m, struct narrow_frame* frame);
void insert_frame_var_lock(struct narrow_frame* frame, uint8_t* address, uint8_t len);
void rm_frame_var_lock(struct narrow_frame* frame, struct found_variable* v);
//_Bool rm_next_frame_var(struct narrow_frame* frame, struct found_variable* v, struct found_variable* rm_first);
void rm_next_frame_var_unsafe(struct narrow_frame* frame, struct found_variable* v, _Bool rm_first);
uint8_t* get_remote_addr(struct mem_map_optimized* m, struct found_variable* v);
void free_mem_map_opt(struct mem_map_optimized* m);

static inline char* type_to_str(enum type_found t) {
    switch(t) {
        case STRING:
            return "STRING";
            break;
        case INT:
            return "INT";
            break;
        case LONG:
            return "LONG";
            break;
        case FLOAT:
            return "FLOAT";
            break;
        case DOUBLE:
            return "DOUBLE";
            break;
        case NONE_T:
            return "NONE";
            break;
    }
    return "NONE";
}

#define p_frame_var(m, f, fmtstr, type) \
    { \
        char pstr[32] = "%p: %"; \
        sprintf(pstr + 5, "%s\n", fmtstr); \
        for (struct found_variable* v = f->tracked_vars; v; v = v->next) { \
            if (!strncmp(fmtstr, "s", 1)) { \
                printf(pstr, get_remote_addr(m, v), (char*)v->address); \
            } else { \
                printf(pstr, get_remote_addr(m, v), *((type*)v->address)); \
            } \
        } \
    }

/* ~~~~~~~~~~~~~~~~end optimized feb 2025 changes~~~~~~~~~~~~~~~~~ */

bool read_bytes_from_pid_mem_dir(void* dest, pid_t pid, int bytes, void* vm_s, void* vm_e);
BYTE* read_bytes_from_pid_mem(pid_t pid, int bytes, void* vm_s, void* vm_e);
int read_single_val_from_pid_mem(pid_t pid, int bytes, void* vm);
char* read_str_from_mem_range(pid_t pid, void* mb_start, int len);
char* read_str_from_mem_range_slow_dir(char* dest, pid_t pid, void* mb_start, int min_strlen, void* last_avail);
char* read_str_from_mem_range_slow(pid_t pid, void* mb_start, void* mb_end);
bool pid_memcpy(pid_t dest_pid, pid_t src_pid, void* dest, void* src, int n_bytes);
bool write_bytes_to_pid_mem(pid_t pid, int bytes, void* vm, BYTE* value);
bool write_int_to_pid_mem(pid_t pid, void* vm, int value);
bool write_str_to_pid_mem(pid_t pid, void* vm, const char* str);
