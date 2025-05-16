#include "vmem_parser.h"

struct framedump{
    // if region >= OTHER, subtract OTHER to get rgn number
    enum m_region region;
    uint64_t address_offset;
    // these are redundant
    enum type_found;
    uint8_t len;
};
