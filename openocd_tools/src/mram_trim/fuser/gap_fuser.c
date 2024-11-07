// This example shows how to program efuses.  //
// This is a permanent operation. Once and efuse is programmed, the same value is
// read, even after power-up. This can be used to remember some settings.
// Be careful that there are some efuses reserved for the ROM to specify the
// boot mode, check the specifications to know which efuses can be used.

#include <pmsis.h>
#include "gap_fuser.h"

static pi_fuser_reg_t *fuser_map_check;

static pi_fuser_reg_t fuser_map[] = {
#ifdef FUSE_BOOT_MRAM
    { .id=0,  .val=0x18 },          // boot from mram
#endif
    { .id=1,  .val=0x800 },         // mram trim enable
    { .id=65, .val=0x7 },           // trim size

    { .id=66, .val=0x0 },           // trim value
    { .id=67, .val=0xFD798D00 },    // trim value
    { .id=68, .val=0x620490D0 },    // trim value
    { .id=69, .val=0x0406082E },    // trim value
    { .id=70, .val=0x0400001B },    // trim value: bit 20-25 are trim val
    { .id=71, .val=0x0 },           // trim value
    { .id=72, .val=0x0 },           // trim value
};

int fuse_trim_val(uint8_t trim_val)
{
    int err = 0;
    printf("Fuser Start\n");

    // Before writing the efuse, we must activate the program operation
    // Once activated, we can wrote as many efuses as we want
    pi_efuse_ioctl(PI_EFUSE_IOCTL_PROGRAM_START, NULL);

    for (uint32_t i=0; i<(sizeof(fuser_map)/sizeof(pi_fuser_reg_t)); i++)
    {
        if (fuser_map[i].id == 70)
        {
            fuser_map[i].val |= ((trim_val & 0x3F)<<20); 
            printf("Fuse the trim val 0x%8X\n", fuser_map[i].val);
        }
        pi_efuse_program(fuser_map[i].id, fuser_map[i].val);
    }

    // Close the current operation once done
    pi_efuse_ioctl(PI_EFUSE_IOCTL_CLOSE, NULL);

    fuser_map_check = (pi_fuser_reg_t *) pi_l2_malloc (sizeof(fuser_map));

    // Before reading the efuse, we must activate the read operation
    // Once activated, we can wrote as many efuses as we want
    pi_efuse_ioctl(PI_EFUSE_IOCTL_READ_START, NULL);

    for (uint32_t i=0; i<(sizeof(fuser_map)/sizeof(pi_fuser_reg_t)); i++)
    {
        fuser_map_check[i].id = fuser_map[i].id;
        fuser_map_check[i].val = pi_efuse_value_get(fuser_map_check[i].id);
    }

    // Close the current operation once done
    pi_efuse_ioctl(PI_EFUSE_IOCTL_CLOSE, NULL);

    for (uint32_t i=0; i<(sizeof(fuser_map)/sizeof(pi_fuser_reg_t)); i++)
    {
        if (fuser_map_check[i].val != fuser_map[i].val)
        {
            printf("[ERR]: Read efuse %d: 0x%x, should be 0x%x\n", fuser_map_check[i].id, fuser_map_check[i].val, fuser_map[i].val);
            err ++;
        }
        else
        {
            printf("[OK]: Read efuse %d: 0x%x, should be 0x%x\n", fuser_map_check[i].id, fuser_map_check[i].val, fuser_map[i].val);
        }
    }

    pi_l2_free (fuser_map_check, sizeof(fuser_map));

    if(err)
        printf("Fuse failed with %d errors\n", err);
    else
        printf("Fuse success \n");

    return err;
}
