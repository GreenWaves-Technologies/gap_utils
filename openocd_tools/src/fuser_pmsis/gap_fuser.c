// This example shows how to program efuses.  //
// This is a permanent operation. Once and efuse is programmed, the same value is
// read, even after power-up. This can be used to remember some settings.
// Be careful that there are some efuses reserved for the ROM to specify the
// boot mode, check the specifications to know which efuses can be used.

#include <pmsis.h>
#include "gap9_fuser_map.h"

#define FUSER_REG_NUM       (128)
#define MRAM_TRIM_OFFSET    (20)

pi_fuser_reg_t *fuser_map_check;

int main(void)
{
    int err = 0;
    printf("Fuser Start\n");
#ifdef DUMP_ONLY

    pi_efuse_ioctl(PI_EFUSE_IOCTL_READ_START, NULL);

    for (int i=0; i<80; i++)
    {
        printf("reg[%d] = 0x%X\n", i, pi_efuse_value_get(i));
    }

    // Close the current operation once done
    pi_efuse_ioctl(PI_EFUSE_IOCTL_CLOSE, NULL);

#else

    printf("size=%d...\n", sizeof(fuser_map)/sizeof(pi_fuser_reg_t));
    // Before writing the efuse, we must activate the program operation
    // Once activated, we can wrote as many efuses as we want
    pi_efuse_ioctl(PI_EFUSE_IOCTL_PROGRAM_START, NULL);

    for (int i=0; i<(sizeof(fuser_map)/sizeof(pi_fuser_reg_t)); i++)
    {
        if (fuser_map[i].id == 70)
        {
            fuser_map[i].val |= (MRAM_TRIM_VAL<<20);
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

    for (int i=0; i<(sizeof(fuser_map)/sizeof(pi_fuser_reg_t)); i++)
    {
        fuser_map_check[i].id = fuser_map[i].id;
        fuser_map_check[i].val = pi_efuse_value_get(fuser_map_check[i].id);
    }

    // Close the current operation once done
    pi_efuse_ioctl(PI_EFUSE_IOCTL_CLOSE, NULL);

    for (int i=0; i<(sizeof(fuser_map)/sizeof(pi_fuser_reg_t)); i++)
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
#endif

    if(err)
        printf("Fuse failed with %d errors\n", err);
    else
        printf("Fuse success \n");

    return err;
}
