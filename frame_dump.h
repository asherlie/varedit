#include "vmem_parser.h"

/*should this be a linked list? don't think so*/
struct vardump{
    enum m_region region;
    uint64_t address_offset;
    uint8_t valsz;
};

/* these will be written to disk */
// TODO: account for byte order
struct framedump{
    char label[16];
    // if region >= OTHER, subtract OTHER to get rgn number
    // TODO: these are redundant, just use sizeof(type found)
    enum type_found type;

    int n_vars;
    struct vardump* vars;
};

void fill_framedump(struct framedump* fdump, struct mem_map_optimized* m, struct narrow_frame* f);
void insert_fd_to_m(struct framedump* fdump, struct mem_map_optimized* m);
