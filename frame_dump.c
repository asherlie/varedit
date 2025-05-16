#include <string.h>

#include "vmem_access.h"
#include "vmem_parser.h"
#include "frame_dump.h"

// TODO: is it safe to access tracked_vars like this?
void fill_framedump(struct framedump* fdump, struct mem_map_optimized* m, struct narrow_frame* f) {
    int idx = 0;
    fdump->type = f->current_type;
    memcpy(fdump->label, f->label, sizeof(fdump->label));
    fdump->n_vars = f->n_tracked;
    fdump->vars = malloc(sizeof(struct vardump) * fdump->n_vars);

    for (struct found_variable* v = f->tracked_vars; v; v = v->next) {
        fdump->vars[idx].address_offset = which_rgn(m->rgn, v->address, &fdump->vars[idx].region);
        ++idx;
    }
}

int main() {
}
