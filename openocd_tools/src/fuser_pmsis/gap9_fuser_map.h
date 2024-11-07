#include "pmsis.h"

pi_fuser_reg_t fuser_map[] = {
#ifdef FUSE_BOOT_MRAM
    { .id=0,  .val=0x18 },          // boot from mram
#endif
#ifdef JTAG_DISABLE
    { .id=0xF,  .val=0x1 },         // jtag disable in disable reg
#endif
#ifdef MRAM_TRIM
    { .id=1,  .val=0x800 },         // mram trim enable
    { .id=65, .val=0x7 },           // trim size

    { .id=66, .val=0x0 },           // trim value
    { .id=67, .val=0xFD798D00 },    // trim value
    { .id=68, .val=0x620490D0 },    // trim value
    { .id=69, .val=0x0406082E },    // trim value
    { .id=70, .val=0x0400001B },    // trim value
    { .id=71, .val=0x0 },           // trim value
    { .id=72, .val=0x0 },           // trim value
#endif
};
