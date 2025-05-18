#include <string.h>
#include <stdio.h>

#include "vmem_access.h"
#include "vmem_parser.h"
#include "frame_dump.h"

// TODO: free up all allocated memory

// TODO: is it safe to access tracked_vars like this?
void fill_framedump(struct framedump* fdump, struct mem_map_optimized* m, struct narrow_frame* f) {
    int idx = 0;
    fdump->type = f->current_type;
    memcpy(fdump->label, f->label, sizeof(fdump->label));
    fdump->n_vars = f->n_tracked;
    fdump->vars = malloc(sizeof(struct vardump) * fdump->n_vars);

    printf("fdump: {%s, type: %i, n: %i}\n", fdump->label, fdump->type, fdump->n_vars);
    for (struct found_variable* v = f->tracked_vars; v; v = v->next) {
        // i don't think which_rgn works unless we convert from local addresses - need to just use m->stack, etc. look into get_remote_addr()
        // my workaround for now is to just call get_remote_addr() before which_rgn()
        // TODO: update get_remote_addr() to have which_rgn() functionality, no reason to call both of these functions
        // i can just as easily compute offsets and region from local addresses, but it's fine to use remote for proof of concept
        fdump->vars[idx].address_offset = which_rgn(m->rgn, get_remote_addr(m, v), &fdump->vars[idx].region);
        // this likely doesn't need to be set for each var
        fdump->vars[idx].valsz = v->len;

        printf("fdump->vars[%i]: {rgn: %i, offset: %li, len: %i}\n", idx, fdump->vars[idx].region, fdump->vars[idx].address_offset, fdump->vars[idx].valsz);
        ++idx;
    }
}

uint8_t* get_local_address(struct vardump* v, struct mem_map_optimized* m) {
    uint8_t* base_addr = NULL;
    if (v->region == STACK) {
        base_addr = m->stack;
    }
    else if (v->region == HEAP) {
        base_addr = m->heap;
    } else if (v->region >= OTHER) {
        base_addr = m->other[v->region - OTHER];
    }

    if (!base_addr) {
        return NULL;
    }

    return base_addr + v->address_offset;
}

// this takes in an empty initialized narrow frame
// maybe this shouldn't return narrow_frame - it should just append frames to m, maybe no need to expose this m stuff
// eh, couldn't hurt to break up code
// this is not threadsafe, just manually altering pointers here
void fd_to_f(struct framedump* fdump, struct mem_map_optimized* m, struct narrow_frame* f) {
    struct found_variable* fv, * prev_fv = NULL;

    memcpy(f->label, fdump->label, sizeof(f->label));
    f->current_type = fdump->type;
    f->n_tracked = fdump->n_vars;

    for (int i = 0; i < fdump->n_vars; ++i) {
        fv = malloc(sizeof(struct found_variable));
        if (prev_fv) {
            prev_fv->next = fv;
        } else {
            f->tracked_vars = fv;
        }
        prev_fv = fv;
        /*frame stores local addresses, SO easy, just add the offest to the region!*/
        fv->address = get_local_address(&fdump->vars[i], m);
        fv->len = fdump->vars[i].valsz;
    }
    fv->next = NULL;
}

void insert_fd_to_m(struct framedump* fdump, struct mem_map_optimized* m) {
    struct narrow_frame* f;
    add_frame(m, fdump->label);

    f = m->frames;
    // for testing, to differentiate
    
    fd_to_f(fdump, m, f);
    *f->label = '@';
}

// writes raw fdump to disk for reading later
_Bool write_framedump_to_disk(struct framedump* fdump, char* dump_label) {
    FILE* fp = fopen(dump_label, "wb");
    size_t nb;

    if (!fp) {
        return 0;
    }
    nb = sizeof(struct framedump) - sizeof(struct vardump*);
    if (fwrite(fdump, 1, nb, fp) != nb) {
        fclose(fp);
        return 0;
    }

    nb = sizeof(struct vardump);
    for (int i = 0; i < fdump->n_vars; ++i) {
        if (fwrite(&fdump->vars[i], 1, nb, fp) != nb) {
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return 1;
}

// loads only work once a search is performed - regions aren't populated yet otherwise
// fix this
struct framedump* load_framedump(char* dump_label) {
    FILE* fp = fopen(dump_label, "rb");
    size_t nb;
    struct framedump* fdump;

    if (!fp) {
        return NULL;
    }

    fdump = malloc(sizeof(struct framedump));

    nb = sizeof(struct framedump) - sizeof(struct vardump*);
    if (fread(fdump, 1, nb, fp) != nb) {
        free(fdump);
        fclose(fp);
        return NULL;
    }

    fdump->vars = malloc(sizeof(struct vardump) * fdump->n_vars);

    nb = sizeof(struct vardump);
    for (int i = 0; i < fdump->n_vars; ++i) {
        if (fread(&fdump->vars[i], 1, nb, fp) != nb) {
            free(fdump);
            fclose(fp);
            return NULL;
        }
    }

    fclose(fp);
    return fdump;
}

_Bool write_frame_to_fdump(struct narrow_frame* f, struct mem_map_optimized* m, char* dump_label) {
    struct framedump fdump;
    fill_framedump(&fdump, m, f);
    return write_framedump_to_disk(&fdump, dump_label);
}

_Bool add_fdump_to_m(char* dump_label, struct mem_map_optimized* m) {
    struct framedump* fdump = load_framedump(dump_label);
    if (!fdump) {
        return 0;
    }

    if (!m->stack && !m->heap && !m->other) {
        populate_mem_map_opt(m);
    }

    insert_fd_to_m(fdump, m);

    return 1;
}
