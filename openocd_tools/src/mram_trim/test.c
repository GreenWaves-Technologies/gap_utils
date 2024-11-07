/*
 * Copyright (C) 2022 GreenWaves Technologies
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms
 * of the BSD license.  See the LICENSE file for details.
 *
 * Authors: Spyros Chiotakis, GreenWaves Technologies (spyros.chiotakis@greenwaves-technologies.com)
 * 
 * Description: DFT test for MRAM booted through JTAG. Read the README.md in the
 *              same folder for more details.
 */

#include "pmsis.h"
#include "stdio.h"
#include <bsp/bsp.h>

#include <bsp/flash/spiflash.h>
#include "gap_fuser.h"

/* Variables used. */
#define NB_PWM_USED      (1)

#define PWM0_FREQ        (200000)

#define PWM0_CH0_DUTY_CYCLE  (50)
pi_device_t pwm[NB_PWM_USED];
struct pi_pwm_conf conf[NB_PWM_USED];

static int32_t pwm_configurations[NB_PWM_USED][4] = {
    /* pwm_id, pwm_ch_id, pwm_freq, pwm_duty_cycle */
    {0, 0, PWM0_FREQ, PWM0_CH0_DUTY_CYCLE},
};


#define MRAM_FREQUENCY 36000000       // Is used only during MRAM reads
                                      // Peripheral clock frequency has to be set accordingly to get
                                      // the frequency defined here. For example, if we put 36.000.000Hz
                                      // for MRAM we need to set periph clock frequency to 360.000.000Hz
                                      // so the integer clock divider divides properly. To set the periph clock frequency
                                      // you need to do: export CONFIG_FREQUENCY_PERIPH=360000000 and 
                                      // export CONFIG_MAX_FREQUENCY_PERIPH=360000000

                                      // Attention the clock will not be exactly 36MHz after. It might
                                      // be 36.3 or 36.4 MHz after FLL stabilizes so its a good idea to look
                                      // in the waves.


#define BUFF_SIZE 4096                // Bytes
#define NVR_SECTOR_SIZE_IN_BYTES 2048 // (2048 / 16) = 128 number of entries per NVR sector
#define BYTES_PER_ENTRY 16            // 16 bytes in a 128bit mram word
#define PROGRAM_SIZE_OTHER ((1 << 12))
#define PROGRAM_SIZE_RTL PROGRAM_SIZE_OTHER
#define FLASH_NAME "MRAM"

// #define QUICK_VERBOSE_TEST 1
// If defined will do 125C test else will do test for
// 25C and -40C
//#define SENSE_AMP_ALGO_125C 1
#define FAILED_BITS_PERMITTED 101
#define FAILED_BITS_PERMITTED_NVR 2

// Set bit[12] pad running to: 0
// Set bit[13] pad done to: 1
// Set bit[14] pad pass/fail to: 0
#define MRAM_TEST_FAILED 0x0002000
// Set bit[12] pad running to: 0
// Set bit[13] pad done to: 1
// Set bit[14] pad pass/fail to: 1
#define MRAM_TEST_PASSED 0x0006000

// We need to send 512 buffers of 4kB each
// Each line is 16 bytes and our memory has 131072 entries
// With each buffer we fill 4096/16 = 256 entries.
// So to fill the memory we need to send 256*512 = 131072
#ifdef QUICK_VERBOSE_TEST
#define NB_ITER 1
#else
#define NB_ITER 512
#endif

unsigned char find_number_of_failed_bits(unsigned char result_received, unsigned char expected_result)
{
    unsigned int number_of_failed_bits = 0;
    unsigned char different_bits;
    different_bits = result_received ^ expected_result; // Compare to see which bits are different by XORing
    number_of_failed_bits = __builtin_popcount(different_bits);

    return number_of_failed_bits;
}

u_int8_t check_results(unsigned char *rx_buffer, unsigned char *tx_buffer, unsigned int *num_of_failed_bits_in_mram_pattern, unsigned int use_ecc)
{
    int number_of_failed_bits_in_word = 0;
    for (int mram_word = 0; mram_word < (BUFF_SIZE / BYTES_PER_ENTRY); mram_word++)
    {
        number_of_failed_bits_in_word = 0;
        for (int mram_word_byte = 0; mram_word_byte < BYTES_PER_ENTRY; mram_word_byte++)
        {
            if (rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] != tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte])
            {
#ifdef QUICK_VERBOSE_TEST
                printf("Incorrect result. Got %x expected %x\n", rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte], tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte]);
#endif
                number_of_failed_bits_in_word = number_of_failed_bits_in_word + find_number_of_failed_bits(rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte], tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte]);
            }
        }
        *num_of_failed_bits_in_mram_pattern = (*num_of_failed_bits_in_mram_pattern) + number_of_failed_bits_in_word;
        if (number_of_failed_bits_in_word > 1)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("Test failed with %d number of failed bits in single word\n", number_of_failed_bits_in_word);
#endif
            return 1;
        }
        if (use_ecc) {
            if (*num_of_failed_bits_in_mram_pattern > 0) {
#ifdef QUICK_VERBOSE_TEST
            printf("Found failed bit when using ECC. Test terminates\n");
#endif
                return 1;
            }
        }
        else {
            if (*num_of_failed_bits_in_mram_pattern > FAILED_BITS_PERMITTED) {
                #ifdef QUICK_VERBOSE_TEST
                printf("Found too many bits failing without ECC. Test terminates\n");
                #endif
                return 1;
            }
        }
    }
    return 0;
}

static inline void get_info(unsigned int *program_size)
{
#if defined(ARCHI_PLATFORM_RTL)
    if (rt_platform() == ARCHI_PLATFORM_RTL)
    {
        *program_size = PROGRAM_SIZE_RTL;
    }
    else
    {
        *program_size = PROGRAM_SIZE_OTHER;
    }
#else
    *program_size = PROGRAM_SIZE_OTHER;
#endif /* __PULP_OS__ */
}

static PI_L2 unsigned char rx_buffer[BUFF_SIZE];
static PI_L2 unsigned char tx_buffer[BUFF_SIZE];

int sector_erase_nvr_and_check(struct pi_device *flash, unsigned int *num_of_failed_bits_in_nvr_pattern)
{
    pi_nvr_access_open(flash);
    uint32_t flash_addr = 0;
    pi_flash_erase(flash, flash_addr, 16); // Temporary fix to delete one sector after
    pi_flash_erase_sector(flash, flash_addr);
    flash_addr = 0x100000;
    pi_flash_erase_sector(flash, flash_addr);

    uint32_t expected_result = 0xFF;


    for (int nvr_addr = 0; nvr_addr <= 0x100000; nvr_addr = nvr_addr + 0x100000)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_read(flash, nvr_addr, rx_buffer, NVR_SECTOR_SIZE_IN_BYTES);

        for (int nvr_word = 0; nvr_word < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); nvr_word++)
        {
            number_of_failed_bits_in_word = 0;
            for (int nvr_word_byte = 0; nvr_word_byte < BYTES_PER_ENTRY; nvr_word_byte++)
            {
                if (rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte] != expected_result)
                {
                    number_of_failed_bits_in_word = number_of_failed_bits_in_word + find_number_of_failed_bits(rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte], expected_result);
                }
            }

            *num_of_failed_bits_in_nvr_pattern += number_of_failed_bits_in_word;
            if (number_of_failed_bits_in_word > 1)
            {
#ifdef QUICK_VERBOSE_TEST
                printf("Too many failed bits in same word NVR\n");
#endif
                return 1;
            }
            if ((*num_of_failed_bits_in_nvr_pattern) > FAILED_BITS_PERMITTED_NVR)
            {
#ifdef QUICK_VERBOSE_TEST
                printf("Too many failed bits in the NVR array\n");
#endif
                return 1;
            }
        }
    }
    pi_nvr_access_close(flash);
    return 0;
}

int main(void)
{
    unsigned int number_of_failed_bits_in_word = 0;
    unsigned int flash_addr = 0;
    // Sum of failed bits after running solid 1/0 pattern and 
    // ckbd, ickbd pattern w/o ECC
    unsigned int sum_of_failed_bits_solid_pattern = 0;
    unsigned int sum_of_failed_bits_solid_pattern_nvr = 0;
    unsigned int sum_of_failed_bits_ckbd_pattern = 0;
    unsigned int sum_of_failed_bits_ckbd_pattern_nvr = 0;
    
    // Describes the total number of failed bits in the memory
    unsigned int num_of_failed_bits_in_mram_pattern = 0;
    unsigned int num_of_failed_bits_in_nvr_pattern = 0;
    struct pi_device flash;

    int test_result = 0;


    // Bit [0]: SW test running
    // Bit [1]: SW test has configured pads to input (Waiting for '0's pattern to be sent)
    // Bit [2]: Waiting for '1's pattern to be sent
    // Bit [3]: Test done
    // Bit [4]: Test pass/fail
    // Bit [8]: SW waits for pattern all '0's to be send from tester (Tester with JTAG sets this bit when all pads are set to '0')
    // Bit [9]: SW waits for pattern all '1's to be send from tester (Tester with JTAG sets this bit when all pads are set to '1')
    volatile unsigned int *boundary_scan_input_status = (unsigned int *)0x1c010000;


    unsigned int *gpio_dir_0_31 = (unsigned int *)0x1a101000;
    *gpio_dir_0_31 = 0x00000003; // Set all to inputs
    unsigned int *gpio_en_0_31 = (unsigned int *)0x1a101004;
    *gpio_en_0_31 = 0x00001ffc; // Enable all GPIOs 
    volatile unsigned int *gpio_input_value_0_31 = (unsigned int *)0x1a101008;

    unsigned int *gpio_dir_32_63 = (unsigned int *)0x1a101048;
    // *gpio_dir_32_63 = 0x00000000; // Set all to inputs
    unsigned int *gpio_en_32_63 = (unsigned int *)0x1a10104C;
    *gpio_en_32_63 = 0xffffffce; // Enable all GPIOs 
    volatile unsigned int *gpio_input_value_32_63 = (unsigned int *)0x1a101050;
    // For the MRAM part of the test
    // GPIO base ( pad i2c2_sda is used to show the test is running,
    //             pad i2c2_scl is used to show if test is done)
    //             pad i3c_sda is used to show if test is pass(1)/fail(0))
    unsigned int *gpio_output_value_32_63 = (unsigned int *)0x1a101054;

    unsigned int *gpio_dir_64_89 = (unsigned int *)0x1a101090;
    *gpio_dir_64_89 = 0x00000000; // Set all to inputs
    unsigned int *gpio_en_64_89 = (unsigned int *)0x1a101094;
    *gpio_en_64_89 = 0x03c00019; // Enable all GPIOs
    // *gpio_en_64_89 = 0x00000019; // Enable all GPIOs 
    volatile unsigned int *gpio_input_value_64_89 = (unsigned int *)0x1a101098;


    // Trim configuration used after the SA pattern
    unsigned int *trim_config = (unsigned int *)0x1A10101C;
    // How many failed bits (FBC) did occur using the trim config above
    unsigned int *trim_failed_bit_count = (unsigned int *)0x1A101020;
    // Which pattern failed. Starting from W/R1 we have
    // 17 patterns in total (1-17)
    unsigned int *failed_pattern = (unsigned int *)0x1A101064;
    // How many bits failed during that pattern
    unsigned int *pattern_failed_bit_count = (unsigned int *)0x1A101068;

    //
    unsigned int *pad_mux_0 = (unsigned int *) 0x1a104010;
    unsigned int *pad_mux_2 = (unsigned int *) 0x1a104018;
    unsigned int *pad_mux_3 = (unsigned int *) 0x1a10401C;
    unsigned int *pad_mux_4 = (unsigned int *) 0x1a104020;
    unsigned int *pad_mux_5 = (unsigned int *) 0x1a104024;

//     // Put all WLCSP pads to GPIO for the tester to test the inputs
    *pad_mux_0 = 0x01555550; // hyper0 pads to GPIO
    *pad_mux_2 = 0x55555054; 
    *pad_mux_3 = 0x55555555; 
    *pad_mux_4 = 0x00000141; 
    *pad_mux_5 = 0x00055000; // wakeup pads to GPIO

    // printf("CHANGING FREQUENCY\n");
    pi_freq_set(PI_FREQ_DOMAIN_FC,     360*1000*1000);
    pi_freq_set(PI_FREQ_DOMAIN_PERIPH, 360*1000*1000);
    // printf("DONE\n");


#ifdef BOUNDARY_SCAN_TEST

#ifdef QUICK_VERBOSE_TEST
    printf("Waiting for tester to set all pads to '1' and inform us\n");
#endif

    // Inform tester (TB) that the boundary scan input test is underway
    // and that we configured the pads as GPIO and wait for '0' input
    // to these pads
    *boundary_scan_input_status = 0x00000003;

    // Wait for tester (TB) to stimulate pads with '0's
    // and inform us that it is done
    while ( ((*boundary_scan_input_status) >> 8) != 1 );

    // Check WLCSP package boundary scan pads values
    if (*gpio_input_value_0_31  != 0x0 ||
        *gpio_input_value_32_63 != 0x0 ||
        *gpio_input_value_64_89 != 0x0) {

        // Set the bit [3] bit to indicate test done
        // and let bit [4] at '0' to indicate test 
        // failed
        #ifdef QUICK_VERBOSE_TEST
        printf("Error found every pad should be '0' but got pads (0-31):%x (32-63):%x (64-89):%x\n", *gpio_input_value_0_31, *gpio_input_value_32_63, *gpio_input_value_64_89);
        #endif
        *boundary_scan_input_status = 0x00000009;
    } else {
        // Test success
        *boundary_scan_input_status = 0x00000019;
    }

#ifdef QUICK_VERBOSE_TEST
    printf("Read the value from JTAG moving on to check\n");
#endif

    // The tester (TB) will unset the result of the test (pass/fail)
    // and will unset the test done bit (bits 3,4)
    while ( ((*boundary_scan_input_status)) != 0x00000001 );

    // Inform tester (TB) that the boundary scan input test is underway
    // and that we configured the pads as GPIO and wait for '1' input
    // to these pads
    *boundary_scan_input_status = 0x00000005;

    // Wait for tester (TB) to stimulate pads with '1's
    // and inform us that it is done
    while ( ((*boundary_scan_input_status) >> 9) != 1 );
    

    // Check WLCSP package boundary scan pads values
    if (*gpio_input_value_0_31  != 0x1ffc ||
        *gpio_input_value_32_63 != 0xffffffce ||
        *gpio_input_value_64_89 != 0x3c00019) {
        #ifdef QUICK_VERBOSE_TEST
        printf("Error found every pad should be '1' but got pads (0-31):%x -> expected 0x1ffc, (32-63):%x -> expected 0xffffffce, (64-89):%x -> expected 0x3c00019\n", *gpio_input_value_0_31, *gpio_input_value_32_63, *gpio_input_value_64_89);
        #endif
        // Set the bit [3] bit to indicate test done
        // and let bit [4] at '0' to indicate test 
        // failed 
        *boundary_scan_input_status = 0x00000008;
    } else {
        // Test success
        *boundary_scan_input_status = 0x00000018;
    }

#endif
    
#ifdef QUICK_VERBOSE_TEST
    printf("Boundary scan input test done!\n");
    printf("Starting DFT MRAM test...\n");
#endif


    *pad_mux_4 = (*pad_mux_4) | 0x15 << 24;
    *gpio_dir_32_63 = (*gpio_dir_32_63) | 0x7 << 12;     // Turn pads to output
    *gpio_en_32_63 = (*gpio_en_32_63) | 0x7 << 12;       // Enable pads
    *gpio_output_value_32_63 = (*gpio_output_value_32_63) | 0x1 << 12; // Set running pad to 1

// *gpio_output_value_32_63 = 0x0000600; // Done & Pass
// *gpio_output_value_32_63 = 0x0000200; // Done & Fail
#ifdef QUICK_VERBOSE_TEST
    printf("Entering main controller (flash: %s)\n", FLASH_NAME);
#endif
    // This pointer writes to a memory location which disactivates the 
    // probing of signals to avoid huge waveform .vcd during test
    int *probe_ptr = (int *)0x1C000000;
    *probe_ptr = 0xAABBCCDD;

    struct pi_mram_conf flash_conf;
    struct pi_flash_info flash_info = {0};

    pi_mram_conf_init(&flash_conf);

    pi_open_from_conf(&flash, &flash_conf);

#ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────────┐ \n"
           " │ Load Initial Trim │ \n"
           " │ into Config Reg   │ \n"
           " └───────────────────┘ \n");
#endif

    if (pi_flash_open(&flash))
        return -1;

    pi_flash_ioctl(&flash, PI_FLASH_IOCTL_SET_BAUDRATE, (void *)MRAM_FREQUENCY);

    pi_mram_ecc_disable(&flash);
    pi_flash_ioctl(&flash, PI_FLASH_IOCTL_INFO, (void *)&flash_info);

#ifdef QUICK_VERBOSE_TEST
    printf(" ┌──────────────┐ \n"
           " │   SA Trim    │ \n"
           " └──────────────┘ \n");
#endif
    unsigned int satm_max = 50;
    unsigned int satm_min = 0;
    unsigned int SA_trim_125C = satm_max;
    unsigned int SA_trim_25C_M40C = satm_min;
    unsigned int satm_offset = 2;
    unsigned int satm_delta = 2;
    unsigned int reftrim = satm_max;
    // Failed Bit Count
    unsigned int FBC = 0;
    // Previous 1
    unsigned int FBC_PRE1 = 0;
    // Previous 2
    unsigned int FBC_PRE2 = 0;
    unsigned int log_FBC = 0;
    // 0.5 ppm(parts per million) ~= 8 allowed errors
    unsigned int fbc_spec_ppm = 8;






// #ifdef QUICK_VERBOSE_TEST
//     pi_flash_erase(&flash, 0, 16); // Temporary fix to delete one sector after
//     pi_flash_erase_sector(&flash, 0);
//     pi_flash_erase_sector(&flash, 8192);
// #else
    pi_flash_erase_chip(&flash);
// #endif




    int checkerboard = 0x55;
    int inverse_checkerboard = 0xAA;
    int pattern_to_use = 0x0;
    for (int mram_word = 0; mram_word < (BUFF_SIZE / BYTES_PER_ENTRY); mram_word++)
    {
        if (mram_word % 2 == 0)
        {
            pattern_to_use = checkerboard;
        }
        else
        {
            pattern_to_use = inverse_checkerboard;
        }
        for (int mram_word_byte = 0; mram_word_byte < BYTES_PER_ENTRY; mram_word_byte++)
        {
            tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] = pattern_to_use;
            rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] = 0;
        }
    }
    // Write the ckbd pattern to memory
    flash_addr = 0x0;
    for (int j = 0; j < 512; j++)
    {
        pi_flash_program(&flash, flash_addr, tx_buffer, BUFF_SIZE);
        flash_addr += BUFF_SIZE;
    }

#ifdef SENSE_AMP_ALGO_125C
    while (1)
    {
        #ifdef QUICK_VERBOSE_TEST
        printf("Reftrim value is: %d\n", reftrim);
        #endif
        pi_mram_sa_trim_config(&flash, reftrim);
        
        FBC = 0;
        flash_addr = 0x0;
        ////////
        for (int i = 0; i < 512; i++) {
            pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);
            for (int mram_word = 0; mram_word < (BUFF_SIZE / BYTES_PER_ENTRY); mram_word++)
            {
                number_of_failed_bits_in_word = 0;
                for (int mram_word_byte = 0; mram_word_byte < BYTES_PER_ENTRY; mram_word_byte++)
                {
                    if (rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] != tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte])
                    {
                        FBC = FBC + find_number_of_failed_bits(rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte], tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte]);
                    }
                }
            }
            flash_addr += BUFF_SIZE;
        }
        ////////


        if (reftrim == satm_min)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("reftrim == satm_min\n");
#endif
            SA_trim_125C = satm_min;
            log_FBC = FBC;
            break;
        }
        else if (FBC <= fbc_spec_ppm)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("FBC <= fbc_spec_ppm\n");
#endif
            SA_trim_125C = reftrim - satm_offset;
            log_FBC = FBC;
            break;
        }
        else if (reftrim == satm_max)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("reftrim == satm_max\n");
#endif
            FBC_PRE1 = FBC;
            reftrim = reftrim - 1;
            continue;
        }
        else if ((unsigned int) __builtin_pulp_abs(FBC_PRE1 - FBC) <= satm_delta)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("abs(FBC_PRE1 - FBC) <= satm_delta\n");
#endif
            SA_trim_125C = reftrim - satm_offset;
            log_FBC = FBC;
            break;
        }
        else if (reftrim == (satm_max - 1))
        {
#ifdef QUICK_VERBOSE_TEST
            printf("reftrim == (satm_max - 1)\n");
#endif
            FBC_PRE2 = FBC_PRE1;
            FBC_PRE1 = FBC;
            reftrim = reftrim - 1;
            continue;
        }
        else if ((FBC > FBC_PRE1) && (FBC_PRE1 > FBC_PRE2))
        {
#ifdef QUICK_VERBOSE_TEST
            printf("FBC > FBC_PRE1 > FBC_PRE2\n");
#endif
            SA_trim_125C = reftrim + 2;
            log_FBC = FBC_PRE2;
            break;
        }
        else
        {
            FBC_PRE2 = FBC_PRE1;
            FBC_PRE1 = FBC;
            reftrim = reftrim - 1;
            continue;
        }
    }
#endif

#ifndef SENSE_AMP_ALGO_125C
    reftrim = satm_min;
    while (1)
    {
        #ifdef QUICK_VERBOSE_TEST
        printf("Reftrim value is: %d\n", reftrim);
        #endif
        pi_mram_sa_trim_config(&flash, reftrim);
        
        FBC = 0;
        flash_addr = 0x0;
        ////////
        for (int i = 0; i < NB_ITER; i++) {
            pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);
            for (int mram_word = 0; mram_word < (BUFF_SIZE / BYTES_PER_ENTRY); mram_word++)
            {
                number_of_failed_bits_in_word = 0;
                for (int mram_word_byte = 0; mram_word_byte < BYTES_PER_ENTRY; mram_word_byte++)
                {
                    if (rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] != tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte])
                    {
    #ifdef QUICK_VERBOSE_TEST
                        printf("Incorrect result. Got %x expected %x\n", rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte], tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte]);
    #endif
                        FBC = FBC + find_number_of_failed_bits(rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte], tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte]);
                    }
                }
            }
            flash_addr += BUFF_SIZE;
        }
        ////////
        if (reftrim == satm_max)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("reftrim == satm_max\n");
#endif
            SA_trim_25C_M40C = satm_max;
            log_FBC = FBC;
            break;
        }
        else if (FBC <= fbc_spec_ppm)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("FBC <= fbc_spec_ppm\n");
#endif
            SA_trim_25C_M40C = reftrim + satm_offset;
            log_FBC = FBC;
            break;
        }
        else if (reftrim == satm_min)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("reftrim == satm_min\n");
#endif
            FBC_PRE1 = FBC;
            reftrim = reftrim + 1;
            continue;
        }
        else if ((unsigned int) __builtin_pulp_abs(FBC_PRE1 - FBC) <= satm_delta)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("abs(FBC_PRE1 - FBC) <= satm_delta\n");
#endif
            SA_trim_25C_M40C = reftrim + satm_offset;
            log_FBC = FBC;
            break;
        }
        else if (reftrim == (satm_min + 1))
        {
#ifdef QUICK_VERBOSE_TEST
            printf("reftrim == (satm_min + 1)\n");
#endif
            FBC_PRE2 = FBC_PRE1;
            FBC_PRE1 = FBC;
            reftrim = reftrim + 1;
            continue;
        }
        else if ((FBC > FBC_PRE1) && (FBC_PRE1 > FBC_PRE2))
        {
#ifdef QUICK_VERBOSE_TEST
            printf("FBC > FBC_PRE1 > FBC_PRE2\n");
#endif
            SA_trim_25C_M40C = reftrim - 2;
            log_FBC = FBC_PRE2;
            break;
        }
        else
        {
            FBC_PRE2 = FBC_PRE1;
            FBC_PRE1 = FBC;
            reftrim = reftrim + 1;
            continue;
        }
    }
    
    #ifdef QUICK_VERBOSE_TEST
        printf(" ┌──────────────────────┐ \n"
               " │ Recall Trim from NVR │ \n"
               " └──────────────────────┘ \n");
    #endif
    uint32_t recalled_125C_trim = 0;
    uint32_t recalled_FBC = 0;
    pi_nvr_access_open(&flash);
    pi_flash_read(&flash, flash_addr, rx_buffer, 2*BYTES_PER_ENTRY);
    pi_nvr_access_close(&flash);
    #ifdef QUICK_VERBOSE_TEST
        printf(" ┌────────────────────┐ \n"
               " │ Load Trim from NVR │ \n"
               " │ into Config Reg    │ \n"
               " └────────────────────┘ \n");
    #endif
    // The 125C reftrim is in the first bits [5:0] of the 1st entry
    recalled_125C_trim = rx_buffer[0] & 0x3F;
    #ifdef QUICK_VERBOSE_TEST
        printf("Recalled 125C reftrim %x\n", recalled_125C_trim);
    #endif
    pi_mram_sa_trim_config(&flash, recalled_125C_trim);
    SA_trim_125C = recalled_125C_trim;
    recalled_FBC = rx_buffer[16];
    recalled_FBC = recalled_FBC | (rx_buffer[17] << 8);
    recalled_FBC = recalled_FBC | (rx_buffer[18] << 16);
    recalled_FBC = recalled_FBC | (rx_buffer[19] << 24);
    log_FBC = recalled_FBC;
#endif



    *trim_config = SA_trim_125C;
    *trim_failed_bit_count = log_FBC;

#ifdef DEBUG_DUMP
    printf("trim value 125: %d\n", SA_trim_125C);
    printf("trim value 25: %d\n", SA_trim_25C_M40C);
#endif

#ifndef SENSE_AMP_ALGO_125C
    uint8_t trim_val_fuse = (SA_trim_125C+SA_trim_25C_M40C)>>1;
    printf("trim val to be fused %d\n", trim_val_fuse);
#ifdef FUSE_TRIM_VALUE
    if (fuse_trim_val(trim_val_fuse))
    {
        printf("Trim fuse failed\n");
    }
#endif
#endif

    #ifdef QUICK_VERBOSE_TEST
        printf(" ┌──────────────┐ \n"
               " │ WR1 w/o ECC  │ \n"
               " └──────────────┘ \n");
    #endif
    
    *failed_pattern = (*failed_pattern) + 1;

    flash_addr = 0;
    for (int i = 0; i < BUFF_SIZE; i++)
    {
        tx_buffer[i] = 0xFF;
        rx_buffer[i] = 0;
    }
    uint8_t test_failed = 0;
    unsigned char expected_result = 0xFF;
    for (int j = 0; j < NB_ITER; j++)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_erase(&flash, flash_addr, BUFF_SIZE);

        //   pi_flash_program(&flash, flash_addr, tx_buffer, BUFF_SIZE);
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 0);
        if (test_failed)
        {
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }

        flash_addr += BUFF_SIZE;
    }

    pi_nvr_access_open(&flash);

    *failed_pattern = (*failed_pattern) + 1;

    for (int nvr_addr = 0; nvr_addr <= 0x100000; nvr_addr = nvr_addr + 0x100000)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_erase(&flash, nvr_addr, NVR_SECTOR_SIZE_IN_BYTES); // Temporary fix to delete one sector after
        pi_flash_read(&flash, nvr_addr, rx_buffer, NVR_SECTOR_SIZE_IN_BYTES);

        for (int nvr_word = 0; nvr_word < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); nvr_word++)
        {
            number_of_failed_bits_in_word = 0;
            for (int nvr_word_byte = 0; nvr_word_byte < BYTES_PER_ENTRY; nvr_word_byte++)
            {
                if (rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte] != tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte])
                {
                    number_of_failed_bits_in_word = number_of_failed_bits_in_word + find_number_of_failed_bits(rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte], tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte]);
                }
            }
            num_of_failed_bits_in_nvr_pattern += number_of_failed_bits_in_word;
            if (number_of_failed_bits_in_word > 1)
            {
                #ifdef QUICK_VERBOSE_TEST
                printf("More than 1 failures in a single row\n");
                #endif
                *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                return -1;
            }
            if (num_of_failed_bits_in_nvr_pattern > FAILED_BITS_PERMITTED_NVR)
            {
                #ifdef QUICK_VERBOSE_TEST
                printf("More than 2 failures in all NVR array\n");
                #endif
                *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                return -1;
            }
        }
    }
    pi_nvr_access_close(&flash);


    // Add them to the sum and check after WR0 pattern is finished
    sum_of_failed_bits_solid_pattern += num_of_failed_bits_in_mram_pattern;
    sum_of_failed_bits_solid_pattern_nvr += num_of_failed_bits_in_nvr_pattern;


    #ifdef QUICK_VERBOSE_TEST
    printf(" ┌──────────────┐ \n"
           " │ WR0 w/o ECC  │ \n"
           " └──────────────┘ \n");
    #endif
    
    *failed_pattern = (*failed_pattern) + 1;

    num_of_failed_bits_in_mram_pattern = 0;
    num_of_failed_bits_in_nvr_pattern = 0;
    flash_addr = 0;
    expected_result = 0x00;
    for (int i = 0; i < BUFF_SIZE; i++)
    {
        tx_buffer[i] = expected_result;
        rx_buffer[i] = 0;
    }
    test_failed = 0;
    for (int j = 0; j < NB_ITER; j++)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_program(&flash, flash_addr, tx_buffer, BUFF_SIZE);
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 0);
        if (test_failed)
        {
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }

        flash_addr += BUFF_SIZE;
    }

    pi_nvr_access_open(&flash);

    *failed_pattern = (*failed_pattern) + 1;

    num_of_failed_bits_in_nvr_pattern = 0;
    for (int nvr_addr = 0; nvr_addr <= 0x100000; nvr_addr = nvr_addr + 0x100000)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_program(&flash, nvr_addr, tx_buffer, BUFF_SIZE);
        pi_flash_read(&flash, nvr_addr, rx_buffer, NVR_SECTOR_SIZE_IN_BYTES);

        for (int nvr_word = 0; nvr_word < (BUFF_SIZE / BYTES_PER_ENTRY); nvr_word++)
        {
            number_of_failed_bits_in_word = 0;
            for (int nvr_word_byte = 0; nvr_word_byte < BYTES_PER_ENTRY; nvr_word_byte++)
            {
                if (rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte] != tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte])
                {
                    number_of_failed_bits_in_word = number_of_failed_bits_in_word + find_number_of_failed_bits(rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte], tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte]);
                }
            }
            num_of_failed_bits_in_nvr_pattern += number_of_failed_bits_in_word;
            if (number_of_failed_bits_in_word > 1)
            {
                #ifdef QUICK_VERBOSE_TEST
                printf("More than 1 failures in a single row\n");
                #endif
                *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                return -1;
            }
            if (num_of_failed_bits_in_nvr_pattern > FAILED_BITS_PERMITTED_NVR)
            {
                #ifdef QUICK_VERBOSE_TEST
                printf("More than 2 failures in all NVR array\n");
                #endif
                *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                return -1;
            }
        }
    }
    pi_nvr_access_close(&flash);

    sum_of_failed_bits_solid_pattern += num_of_failed_bits_in_mram_pattern;
    sum_of_failed_bits_solid_pattern_nvr += num_of_failed_bits_in_nvr_pattern;

    if (sum_of_failed_bits_solid_pattern > FAILED_BITS_PERMITTED) {
        #ifdef QUICK_VERBOSE_TEST
        printf("Too many bits failed after running the sum of solid 1/0 pattern test\n");
        #endif
        *pattern_failed_bit_count = sum_of_failed_bits_solid_pattern;
        *gpio_output_value_32_63 = MRAM_TEST_FAILED;
        return -1;
    }
    
    if (sum_of_failed_bits_solid_pattern_nvr > FAILED_BITS_PERMITTED_NVR) {
        #ifdef QUICK_VERBOSE_TEST
        printf("Too many bits failed after running the sum of solid 1/0 pattern test in nvr\n");
        #endif
        *pattern_failed_bit_count = sum_of_failed_bits_solid_pattern_nvr;
        *gpio_output_value_32_63 = MRAM_TEST_FAILED;
        return -1;
    }

    num_of_failed_bits_in_mram_pattern = 0;
    num_of_failed_bits_in_nvr_pattern = 0;


    pi_mram_ecc_enable(&flash);



    #ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────┐ \n"
           " │ W/R Walking 1 │ \n"
           " │    w/ ECC     │ \n"
           " └───────────────┘ \n");
    #endif

    *failed_pattern = (*failed_pattern) + 1;

#ifdef QUICK_VERBOSE_TEST
    pi_flash_erase(&flash, 0, 16); // Temporary fix to delete one sector after
    pi_flash_erase_sector(&flash, 0);
    pi_flash_erase_sector(&flash, 8192);
#else
    pi_flash_erase_chip(&flash);
#endif

    flash_addr = 0;
    uint32_t row_shift = 0;
    uint32_t word_shift = 0;
    uint32_t entry_to_shift = 0;
    // In the loop every 16 shift by 1 to the right
    for (uint32_t i = 0; i < (BUFF_SIZE/BYTES_PER_ENTRY); i++)
    {
        entry_to_shift = 15 - (((word_shift+row_shift)%128) / 8); // Every 8 transfers switch to next byte (+ row_shift here also)
        for (uint32_t j = 0; j < BYTES_PER_ENTRY; j++)
        {
            if (entry_to_shift == j) {
                tx_buffer[(i*BYTES_PER_ENTRY)+j] = 0x80 >> ((word_shift+row_shift)%8); // + row_shift
            }
            else {
                tx_buffer[(i*BYTES_PER_ENTRY)+j] = 0x00;
            }
            rx_buffer[(i*BYTES_PER_ENTRY)+j] = 0;
        }
        word_shift++;
        if (word_shift == 128)
        {
            word_shift = 0;
            row_shift++;
        }
        if (row_shift == 128)
        {
            row_shift = 0;
        }
    }

    test_failed = 0;
    expected_result = 0xFF;
    for (int iter = 0; iter < NB_ITER; iter++)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_program(&flash, flash_addr, tx_buffer, BUFF_SIZE);
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        for (int i = 0; i < (BUFF_SIZE/BYTES_PER_ENTRY); i++)
        {
            for (int j = 0; j < BYTES_PER_ENTRY; j++)
            {
                if (rx_buffer[(i*BYTES_PER_ENTRY)+j] != tx_buffer[(i*BYTES_PER_ENTRY)+j]) {
                    #ifdef QUICK_VERBOSE_TEST
                    printf("Found a failed bit test failed\n");
                    #endif
                    num_of_failed_bits_in_mram_pattern = find_number_of_failed_bits(rx_buffer[(i * BYTES_PER_ENTRY) + j], tx_buffer[(i * BYTES_PER_ENTRY) + j]);
                    *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
                    *gpio_output_value_32_63 = MRAM_TEST_FAILED; 
                    return -1;
                }
            }
        }

        // In the loop every 16 shift by 1 to the right
        for (int i = 0; i < (BUFF_SIZE/BYTES_PER_ENTRY); i++)
        {
            entry_to_shift = 15 - (((word_shift+row_shift)%128) / 8); // Every 8 transfers switch to next byte (+ row_shift here also)
            for (uint32_t j = 0; j < BYTES_PER_ENTRY; j++)
            {
                if (entry_to_shift == j) {
                    tx_buffer[(i*BYTES_PER_ENTRY)+j] = 0x80 >> ((word_shift+row_shift)%8); // + row_shift
                }
                else {
                    tx_buffer[(i*BYTES_PER_ENTRY)+j] = 0x00;
                }
                rx_buffer[(i*BYTES_PER_ENTRY)+j] = 0;
            }
            word_shift++;
            if (word_shift == 128)
            {
                word_shift = 0;
                row_shift++;
            }
            if (row_shift == 128)
            {
                row_shift = 0;
            }
        }
        flash_addr += BUFF_SIZE;
    }

    pi_nvr_access_open(&flash);

    *failed_pattern = (*failed_pattern) + 1;

    // Clear NVR sectors
    flash_addr = 0;
    pi_flash_erase(&flash, flash_addr, 16); // Temporary fix to delete one sector after
    pi_flash_erase_sector(&flash, flash_addr);
    flash_addr = 0x100000;
    pi_flash_erase_sector(&flash, flash_addr);

    flash_addr = 0;
    row_shift = 0;
    word_shift = 0;
    entry_to_shift = 0;
    // In the loop every 16 shift by 1 to the right
    for (int i = 0; i < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); i++)
    {
        entry_to_shift = 15 - (((word_shift + row_shift) % 128) / 8); // Every 8 transfers switch to next byte (+ row_shift here also)
        for (uint32_t j = 0; j < BYTES_PER_ENTRY; j++)
        {
            if (entry_to_shift == j)
            {
                tx_buffer[(i * BYTES_PER_ENTRY) + j] = 0x80 >> ((word_shift+row_shift)%8); // + row_shift
            }
            else
            {
                tx_buffer[(i * BYTES_PER_ENTRY) + j] = 0xFF;
            }
            rx_buffer[(i * BYTES_PER_ENTRY) + j] = 0;
        }
        word_shift++;
        if (word_shift == 128)
        {
            word_shift = 0;
            row_shift++;
        }
        if (row_shift == 128)
        {
            row_shift = 0;
        }
    }

    for (int nvr_addr = 0; nvr_addr <= 0x100000; nvr_addr = nvr_addr + 0x100000)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_program(&flash, nvr_addr, tx_buffer, NVR_SECTOR_SIZE_IN_BYTES);
        pi_flash_read(&flash, nvr_addr, rx_buffer, NVR_SECTOR_SIZE_IN_BYTES);

        for (int nvr_word = 0; nvr_word < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); nvr_word++)
        {
            number_of_failed_bits_in_word = 0;
            for (int nvr_word_byte = 0; nvr_word_byte < BYTES_PER_ENTRY; nvr_word_byte++)
            {
                if (rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte] != tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte])
                {
#ifdef QUICK_VERBOSE_TEST
                    printf("Found failed bit in the nvr test failed!\n");
#endif
                    num_of_failed_bits_in_nvr_pattern = find_number_of_failed_bits(rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte], tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte]);
                    *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                    *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                    return -1;
                }
            }
        }
        // In the loop every 16 shift by 1 to the right
        for (int i = 0; i < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); i++)
        {
            entry_to_shift = 15 - (((word_shift + row_shift) % 128) / 8); // Every 8 transfers switch to next byte (+ row_shift here also)
            for (uint32_t j = 0; j < BYTES_PER_ENTRY; j++)
            {
                if (entry_to_shift == j)
                {
                    tx_buffer[(i * BYTES_PER_ENTRY) + j] = 0x80 >> ((word_shift+row_shift)%8); // + row_shift
                }
                else
                {
                    tx_buffer[(i * BYTES_PER_ENTRY) + j] = 0xFF;
                }
                rx_buffer[(i * BYTES_PER_ENTRY) + j] = 0;
            }
            word_shift++;
            if (word_shift == 128)
            {
                word_shift = 0;
                row_shift++;
            }
            if (row_shift == 128)
            {
                row_shift = 0;
            }
        }
    }
    pi_nvr_access_close(&flash);








#ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────┐ \n"
           " │ W/R Walking 0 │ \n"
           " │    w/ ECC     │ \n"
           " └───────────────┘ \n");
#endif

    *failed_pattern = (*failed_pattern) + 1;

#ifdef QUICK_VERBOSE_TEST
    pi_flash_erase(&flash, 0, 16); // Temporary fix to delete one sector after
    pi_flash_erase_sector(&flash, 0);
    pi_flash_erase_sector(&flash, 8192);
#else
    pi_flash_erase_chip(&flash);
#endif

    flash_addr = 0;
    row_shift = 0;
    word_shift = 0;
    entry_to_shift = 0;
    num_of_failed_bits_in_mram_pattern = 0;
    num_of_failed_bits_in_nvr_pattern = 0;
    // In the loop every 16 shift by 1 to the right
    for (uint32_t i = 0; i < (BUFF_SIZE / BYTES_PER_ENTRY); i++)
    {
        entry_to_shift = 15 - (((word_shift + row_shift) % 128) / 8); // Every 8 transfers switch to next byte (+ row_shift here also)
        for (uint32_t j = 0; j < BYTES_PER_ENTRY; j++)
        {
            if (entry_to_shift == j)
            {
                tx_buffer[(i * BYTES_PER_ENTRY) + j] = ~(0x80 >> ((word_shift + row_shift) % 8)); // + row_shift
            }
            else
            {
                tx_buffer[(i * BYTES_PER_ENTRY) + j] = 0xFF;
            }
            rx_buffer[(i * BYTES_PER_ENTRY) + j] = 0;
        }
        word_shift++;
        if (word_shift == 128)
        {
            word_shift = 0;
            row_shift++;
        }
        if (row_shift == 128)
        {
            row_shift = 0;
        }
    }

    test_failed = 0;
    expected_result = 0xFF;
    for (int iter = 0; iter < NB_ITER; iter++)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_program(&flash, flash_addr, tx_buffer, BUFF_SIZE);
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        for (int i = 0; i < (BUFF_SIZE / BYTES_PER_ENTRY); i++)
        {
            for (int j = 0; j < BYTES_PER_ENTRY; j++)
            {
                if (rx_buffer[(i * BYTES_PER_ENTRY) + j] != tx_buffer[(i * BYTES_PER_ENTRY) + j]) {
                    #ifdef QUICK_VERBOSE_TEST
                    printf("Found failed bit in the emram. Test failed!\n");
                    #endif
                    num_of_failed_bits_in_mram_pattern = find_number_of_failed_bits(rx_buffer[(i * BYTES_PER_ENTRY) + j], tx_buffer[(i * BYTES_PER_ENTRY) + j]);
                    *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
                    *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                    return -1;
                }
            }
        }

        // In the loop every 16 shift by 1 to the right
        for (int i = 0; i < (BUFF_SIZE / BYTES_PER_ENTRY); i++)
        {
            entry_to_shift = 15 - (((word_shift + row_shift) % 128) / 8); // Every 8 transfers switch to next byte (+ row_shift here also)
            for (uint32_t j = 0; j < BYTES_PER_ENTRY; j++)
            {
                if (entry_to_shift == j)
                {
                    tx_buffer[(i * BYTES_PER_ENTRY) + j] = ~(0x80 >> ((word_shift + row_shift) % 8)); // + row_shift
                }
                else
                {
                    tx_buffer[(i * BYTES_PER_ENTRY) + j] = 0xFF;
                }
                rx_buffer[(i * BYTES_PER_ENTRY) + j] = 0;
            }
            word_shift++;
            if (word_shift == 128)
            {
                word_shift = 0;
                row_shift++;
            }
            if (row_shift == 128)
            {
                row_shift = 0;
            }
        }
        flash_addr += BUFF_SIZE;
    }

    pi_nvr_access_open(&flash);

    *failed_pattern = (*failed_pattern) + 1;

    // Clear NVR sectors
    flash_addr = 0;
    pi_flash_erase(&flash, flash_addr, 16); // Temporary fix to delete one sector after
    pi_flash_erase_sector(&flash, flash_addr);
    flash_addr = 0x100000;
    pi_flash_erase_sector(&flash, flash_addr);

    flash_addr = 0;
    row_shift = 0;
    word_shift = 0;
    entry_to_shift = 0;
    // In the loop every 16 shift by 1 to the right
    for (int i = 0; i < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); i++)
    {
        entry_to_shift = 15 - (((word_shift + row_shift) % 128) / 8); // Every 8 transfers switch to next byte (+ row_shift here also)
        for (uint32_t j = 0; j < BYTES_PER_ENTRY; j++)
        {
            if (entry_to_shift == j)
            {
                tx_buffer[(i * BYTES_PER_ENTRY) + j] = ~(0x80 >> ((word_shift + row_shift) % 8)); // + row_shift
            }
            else
            {
                tx_buffer[(i * BYTES_PER_ENTRY) + j] = 0xFF;
            }
            rx_buffer[(i * BYTES_PER_ENTRY) + j] = 0;
        }
        word_shift++;
        if (word_shift == 128)
        {
            word_shift = 0;
            row_shift++;
        }
        if (row_shift == 128)
        {
            row_shift = 0;
        }
    }

    for (int nvr_addr = 0; nvr_addr <= 0x100000; nvr_addr = nvr_addr + 0x100000)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_program(&flash, nvr_addr, tx_buffer, NVR_SECTOR_SIZE_IN_BYTES);
        pi_flash_read(&flash, nvr_addr, rx_buffer, NVR_SECTOR_SIZE_IN_BYTES);

        for (int nvr_word = 0; nvr_word < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); nvr_word++)
        {
            number_of_failed_bits_in_word = 0;
            for (int nvr_word_byte = 0; nvr_word_byte < BYTES_PER_ENTRY; nvr_word_byte++)
            {
                if (rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte] != tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte])
                {
#ifdef QUICK_VERBOSE_TEST
                    printf("Found failed bit in the nvr test failed!\n");
#endif
                    num_of_failed_bits_in_nvr_pattern = find_number_of_failed_bits(rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte], tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte]);
                    *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                    *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                    return -1;
                }
            }
        }
        // In the loop every 16 shift by 1 to the right
        for (int i = 0; i < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); i++)
        {
            entry_to_shift = 15 - (((word_shift + row_shift) % 128) / 8); // Every 8 transfers switch to next byte (+ row_shift here also)
            for (uint32_t j = 0; j < BYTES_PER_ENTRY; j++)
            {
                if (entry_to_shift == j)
                {
                    tx_buffer[(i * BYTES_PER_ENTRY) + j] = ~(0x80 >> ((word_shift + row_shift) % 8)); // + row_shift
                }
                else
                {
                    tx_buffer[(i * BYTES_PER_ENTRY) + j] = 0xFF;
                }
                rx_buffer[(i * BYTES_PER_ENTRY) + j] = 0;
            }
            word_shift++;
            if (word_shift == 128)
            {
                word_shift = 0;
                row_shift++;
            }
            if (row_shift == 128)
            {
                row_shift = 0;
            }
        }
    }
    pi_nvr_access_close(&flash);




    pi_mram_ecc_disable(&flash);




#ifdef QUICK_VERBOSE_TEST
    printf(" ┌────────────────────┐ \n"
           " │ Chip Erase w/o ECC │ \n"
           " │      (Array)       │ \n"
           " └────────────────────┘ \n");
#endif

    *failed_pattern = (*failed_pattern) + 1;
    
    num_of_failed_bits_in_mram_pattern = 0;
    num_of_failed_bits_in_nvr_pattern = 0;
    for (int i = 0; i < BUFF_SIZE; i++)
    {
        tx_buffer[i] = 0xFF;
        rx_buffer[i] = 0;
    }
    flash_addr = 0;
#ifdef QUICK_VERBOSE_TEST
    pi_flash_erase(&flash, 0, 16); // Temporary fix to delete one sector after
    pi_flash_erase_sector(&flash, 0);
    pi_flash_erase_sector(&flash, 8192);
#else
    pi_flash_erase_chip(&flash);
#endif
    for (int j = 0; j < NB_ITER; j++)
    {
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 0);
        if (test_failed)
        {
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }

        flash_addr += BUFF_SIZE;
    }
    num_of_failed_bits_in_mram_pattern = 0;




#ifdef QUICK_VERBOSE_TEST
    printf(" ┌──────────────────┐ \n"
           " │ Sector Erase NVR │ \n"
           " │      w/o ECC     │ \n"
           " └──────────────────┘ \n");
#endif

    *failed_pattern = (*failed_pattern) + 1;
    
    num_of_failed_bits_in_nvr_pattern = 0;
    test_result = sector_erase_nvr_and_check(&flash, &num_of_failed_bits_in_nvr_pattern);

    if (test_result != 0)
    {
#ifdef QUICK_VERBOSE_TEST
        printf("Test failed\n");
#endif
        *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
        *gpio_output_value_32_63 = MRAM_TEST_FAILED;
        return -1;
    }
    num_of_failed_bits_in_nvr_pattern = 0;



#ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────────┐ \n"
           " │ W/R Ckbd w/o ECC  │ \n"
           " └───────────────────┘ \n");
#endif
    *failed_pattern = (*failed_pattern) + 1;
    
    num_of_failed_bits_in_mram_pattern = 0;
    num_of_failed_bits_in_nvr_pattern = 0;

    checkerboard = 0x55;
    inverse_checkerboard = 0xAA;
    pattern_to_use = 0x0;
    for (int mram_word = 0; mram_word < (BUFF_SIZE / BYTES_PER_ENTRY); mram_word++)
    {
        if (mram_word % 2 == 0)
        {
            pattern_to_use = checkerboard;
        }
        else
        {
            pattern_to_use = inverse_checkerboard;
        }
        for (int mram_word_byte = 0; mram_word_byte < BYTES_PER_ENTRY; mram_word_byte++)
        {
            tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] = pattern_to_use;
            rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] = 0;
        }
    }

    flash_addr = 0x0;
    for (int j = 0; j < NB_ITER; j++)
    {
        pi_flash_program(&flash, flash_addr, tx_buffer, BUFF_SIZE);
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 0);
        if (test_failed)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("More than one bit dead in the same word\n");
#endif
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }

        flash_addr += BUFF_SIZE;
    }

    pi_nvr_access_open(&flash);

    *failed_pattern = (*failed_pattern) + 1;

    for (int nvr_addr = 0; nvr_addr <= 0x100000; nvr_addr = nvr_addr + 0x100000)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_program(&flash, nvr_addr, tx_buffer, NVR_SECTOR_SIZE_IN_BYTES);
        pi_flash_read(&flash, nvr_addr, rx_buffer, NVR_SECTOR_SIZE_IN_BYTES);

        for (int nvr_word = 0; nvr_word < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); nvr_word++)
        {
            number_of_failed_bits_in_word = 0;
            for (int nvr_word_byte = 0; nvr_word_byte < BYTES_PER_ENTRY; nvr_word_byte++)
            {
                if (rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte] != tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte])
                {
                    number_of_failed_bits_in_word = number_of_failed_bits_in_word + find_number_of_failed_bits(rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte], tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte]);
                }
            }
            num_of_failed_bits_in_nvr_pattern += number_of_failed_bits_in_word;
            if (num_of_failed_bits_in_nvr_pattern > FAILED_BITS_PERMITTED_NVR)
            {
                #ifdef QUICK_VERBOSE_TEST
                printf("3 or more failures in multiple parts of NVR\n");
                #endif
                *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                return -1;
            }
            if (number_of_failed_bits_in_word > 1)
            {
                #ifdef QUICK_VERBOSE_TEST
                printf("More than 1 failures in a single row\n");
                #endif
                *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                return -1;
            }
        }
    }
    pi_nvr_access_close(&flash);


    sum_of_failed_bits_ckbd_pattern += num_of_failed_bits_in_mram_pattern;
    sum_of_failed_bits_ckbd_pattern_nvr += num_of_failed_bits_in_nvr_pattern;








#ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────┐ \n"
           " │ Sector Erase  │ \n"
           " │    w/o ECC    │ \n"
           " └───────────────┘ \n");
#endif
    
    *failed_pattern = (*failed_pattern) + 1;
    num_of_failed_bits_in_mram_pattern = 0;
    num_of_failed_bits_in_nvr_pattern = 0;
    
    flash_addr = 0;
    expected_result = 0xFF;

    for (int i = 0; i < BUFF_SIZE; i++)
    {
        tx_buffer[i] = expected_result;
        rx_buffer[i] = 0;
    }

#ifdef QUICK_VERBOSE_TEST
    pi_flash_erase(&flash, 0, 16); // Temporary fix to delete one sector after
    pi_flash_erase_sector(&flash, 0);
    pi_flash_erase_sector(&flash, 8192);
#else
    pi_flash_erase(&flash, flash_addr, 16); // Temporary fix to delete one sector after
    // 8192 because mram has 256 sectors and each sector has 512 words of 16 bytes. 512*16=8192
    for (int sector_addr = 0; sector_addr < 256 * 8192; sector_addr = sector_addr + 8192)
    {
        pi_flash_erase_sector(&flash, sector_addr);
    }

#endif


    test_failed = 0;

    for (int j = 0; j < NB_ITER; j++)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 0);
        if (test_failed)
        {
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }

        flash_addr += BUFF_SIZE;
    }
    if (num_of_failed_bits_in_mram_pattern > FAILED_BITS_PERMITTED)
    {
#ifdef QUICK_VERBOSE_TEST
        printf("Test failed with %d failed bits in total\n", num_of_failed_bits_in_mram_pattern);
#endif
        *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
        *gpio_output_value_32_63 = MRAM_TEST_FAILED;
        return -1;
    }


    num_of_failed_bits_in_nvr_pattern = 0;
    test_result = 0;
    *failed_pattern = (*failed_pattern) + 1;
    test_result = sector_erase_nvr_and_check(&flash, &num_of_failed_bits_in_nvr_pattern);

    // W/O ECC we are more tolerant to bit errors
    if (test_result != 0)
    {
#ifdef QUICK_VERBOSE_TEST
        printf("Test failed\n");
#endif
        *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
        *gpio_output_value_32_63 = MRAM_TEST_FAILED;
        return -1;
    }
    num_of_failed_bits_in_nvr_pattern = 0;
















#ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────────┐ \n"
           " │ W/R iCkbd w/o ECC │ \n"
           " └───────────────────┘ \n");
#endif

    *failed_pattern = (*failed_pattern) + 1;

    num_of_failed_bits_in_mram_pattern = 0;
    num_of_failed_bits_in_nvr_pattern = 0;

    checkerboard = 0x55;
    inverse_checkerboard = 0xAA;
    pattern_to_use = 0x0;
    for (int mram_word = 0; mram_word < (BUFF_SIZE / BYTES_PER_ENTRY); mram_word++)
    {
        if (mram_word % 2 == 0)
        {
            pattern_to_use = inverse_checkerboard;
        }
        else
        {
            pattern_to_use = checkerboard;
        }
        for (int mram_word_byte = 0; mram_word_byte < BYTES_PER_ENTRY; mram_word_byte++)
        {
            tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] = pattern_to_use;
            rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] = 0;
        }
    }

    flash_addr = 0x0;
    for (int j = 0; j < NB_ITER; j++)
    {
        pi_flash_program(&flash, flash_addr, tx_buffer, BUFF_SIZE);
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 0);
        if (test_failed)
        {
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }

        flash_addr += BUFF_SIZE;
    }

    pi_nvr_access_open(&flash);

    *failed_pattern = (*failed_pattern) + 1;

    for (int nvr_addr = 0; nvr_addr <= 0x100000; nvr_addr = nvr_addr + 0x100000)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_program(&flash, nvr_addr, tx_buffer, NVR_SECTOR_SIZE_IN_BYTES);
        pi_flash_read(&flash, nvr_addr, rx_buffer, NVR_SECTOR_SIZE_IN_BYTES);

        for (int nvr_word = 0; nvr_word < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); nvr_word++)
        {
            number_of_failed_bits_in_word = 0;
            for (int nvr_word_byte = 0; nvr_word_byte < BYTES_PER_ENTRY; nvr_word_byte++)
            {
                if (rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte] != tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte])
                {
                    number_of_failed_bits_in_word = number_of_failed_bits_in_word + find_number_of_failed_bits(rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte], tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte]);
                }
            }
            num_of_failed_bits_in_nvr_pattern += number_of_failed_bits_in_word;
            if (num_of_failed_bits_in_nvr_pattern > FAILED_BITS_PERMITTED_NVR)
            {
                #ifdef QUICK_VERBOSE_TEST
                printf("3 or more failed bits in multiple NVR rows\n");
                #endif
                *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                return -1;
            }
            if (number_of_failed_bits_in_word > 1)
            {
                #ifdef QUICK_VERBOSE_TEST
                printf("More than 1 failures in a single row\n");
                #endif
                *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                return -1;
            }
        }
    }
    pi_nvr_access_close(&flash);



    sum_of_failed_bits_ckbd_pattern += num_of_failed_bits_in_mram_pattern;
    sum_of_failed_bits_ckbd_pattern_nvr += num_of_failed_bits_in_nvr_pattern;


    if (sum_of_failed_bits_ckbd_pattern > FAILED_BITS_PERMITTED)
    {
        #ifdef QUICK_VERBOSE_TEST
        printf("Sum of failed bits in ckbd and ickbd patterns in MRAM is greater than the failed bits permitted. Test failed\n");
        #endif
        *pattern_failed_bit_count = sum_of_failed_bits_ckbd_pattern;
        *gpio_output_value_32_63 = MRAM_TEST_FAILED;
        return -1;
    }
    if (sum_of_failed_bits_ckbd_pattern_nvr > FAILED_BITS_PERMITTED_NVR)
    {
        #ifdef QUICK_VERBOSE_TEST
        printf("Sum of failed bits in ckbd and ickbd patterns in NVR is greater than the failed bits permitted. Test failed\n");
        #endif
        *pattern_failed_bit_count = sum_of_failed_bits_ckbd_pattern_nvr;
        *gpio_output_value_32_63 = MRAM_TEST_FAILED;
        return -1;
    }




    pi_mram_ecc_enable(&flash);








#ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────────┐ \n"
           " │ Chip Erase w/ ECC │ \n"
           " │      (Array)      │ \n"
           " └───────────────────┘ \n");
#endif


    *failed_pattern = (*failed_pattern) + 1;

    num_of_failed_bits_in_mram_pattern = 0;
    num_of_failed_bits_in_nvr_pattern = 0;

    for (int i = 0; i < BUFF_SIZE; i++)
    {
        tx_buffer[i] = 0xFF;
        rx_buffer[i] = 0;
    }
    flash_addr = 0;
#ifdef QUICK_VERBOSE_TEST
    pi_flash_erase(&flash, 0, 16); // Temporary fix to delete one sector after
    pi_flash_erase_sector(&flash, 0);
    pi_flash_erase_sector(&flash, 8192);
#else
    pi_flash_erase_chip(&flash);
#endif
    for (int j = 0; j < NB_ITER; j++)
    {
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 1);
        if (test_failed)
        {
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }

        flash_addr += BUFF_SIZE;
    }










#ifdef QUICK_VERBOSE_TEST
    printf(" ┌──────────────────┐ \n"
           " │ Sector Erase NVR │ \n"
           " │      w/ ECC      │ \n"
           " └──────────────────┘ \n");
#endif

    *failed_pattern = (*failed_pattern) + 1;

    num_of_failed_bits_in_nvr_pattern = 0;
    test_result = 0;
    sector_erase_nvr_and_check(&flash, &num_of_failed_bits_in_nvr_pattern);

    if (num_of_failed_bits_in_nvr_pattern > 0)
    {
#ifdef QUICK_VERBOSE_TEST
        printf("NVR memory has at least one bit failing. Number of failed bits %d\n", num_of_failed_bits_in_nvr_pattern);
#endif
        *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
        *gpio_output_value_32_63 = MRAM_TEST_FAILED;
        return -1;
    }
    num_of_failed_bits_in_nvr_pattern = 0;










#ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────────┐ \n"
           " │ W/R Ckbd w/ ECC   │ \n"
           " └───────────────────┘ \n");
#endif

    *failed_pattern = (*failed_pattern) + 1;

    checkerboard = 0x55;
    inverse_checkerboard = 0xAA;
    pattern_to_use = 0x0;
    for (int mram_word = 0; mram_word < (BUFF_SIZE / BYTES_PER_ENTRY); mram_word++)
    {
        if (mram_word % 2 == 0)
        {
            pattern_to_use = checkerboard;
        }
        else
        {
            pattern_to_use = inverse_checkerboard;
        }
        for (int mram_word_byte = 0; mram_word_byte < BYTES_PER_ENTRY; mram_word_byte++)
        {
            tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] = pattern_to_use;
            rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] = 0;
        }
    }

    flash_addr = 0x0;
    for (int j = 0; j < NB_ITER; j++)
    {
        pi_flash_program(&flash, flash_addr, tx_buffer, BUFF_SIZE);
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 1);
        if (test_failed)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("Found at least 1 bit failing with ECC on. Test failed\n");
#endif
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }

        flash_addr += BUFF_SIZE;
    }

    pi_nvr_access_open(&flash);

    *failed_pattern = (*failed_pattern) + 1;

    for (int nvr_addr = 0; nvr_addr <= 0x100000; nvr_addr = nvr_addr + 0x100000)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_program(&flash, nvr_addr, tx_buffer, NVR_SECTOR_SIZE_IN_BYTES);
        pi_flash_read(&flash, nvr_addr, rx_buffer, NVR_SECTOR_SIZE_IN_BYTES);

        for (int nvr_word = 0; nvr_word < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); nvr_word++)
        {
            number_of_failed_bits_in_word = 0;
            for (int nvr_word_byte = 0; nvr_word_byte < BYTES_PER_ENTRY; nvr_word_byte++)
            {
                if (rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte] != tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte])
                {
                    number_of_failed_bits_in_word = number_of_failed_bits_in_word + find_number_of_failed_bits(rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte], tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte]);
                    #ifdef QUICK_VERBOSE_TEST
                    printf("At least 1 bit failed while using ECC in NVR\n");
                    #endif
                    *pattern_failed_bit_count = number_of_failed_bits_in_word;
                    *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                    return -1;
                }
            }
        }
    }
    pi_nvr_access_close(&flash);










#ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────┐ \n"
           " │ Sector Erase  │ \n"
           " │    w/ ECC     │ \n"
           " └───────────────┘ \n");
#endif

    *failed_pattern = (*failed_pattern) + 1;

    num_of_failed_bits_in_mram_pattern = 0;
    num_of_failed_bits_in_nvr_pattern = 0;

    flash_addr = 0;
    expected_result = 0xFF;

    for (int i = 0; i < BUFF_SIZE; i++)
    {
        tx_buffer[i] = expected_result;
        rx_buffer[i] = 0;
    }
    num_of_failed_bits_in_mram_pattern = 0;

#ifdef QUICK_VERBOSE_TEST
    pi_flash_erase(&flash, 0, 16); // Temporary fix to delete one sector after
    pi_flash_erase_sector(&flash, 0);
    pi_flash_erase_sector(&flash, 8192);
#else
    pi_flash_erase(&flash, flash_addr, 16); // Temporary fix to delete one sector after
    // 8192 because mram has 256 sectors and each sector has 512 words of 16 bytes. 512*16=8192
    for (int sector_addr = 0; sector_addr < 256 * 8192; sector_addr = sector_addr + 8192)
    {
        pi_flash_erase_sector(&flash, sector_addr);
    }
#endif



    test_failed = 0;

    for (int j = 0; j < NB_ITER; j++)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 1);
        if (test_failed)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("Found at least 1 bit failing with ECC on. Test failed\n");
#endif
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }

        flash_addr += BUFF_SIZE;
    }


    num_of_failed_bits_in_nvr_pattern = 0;
    test_result = 0;
    *failed_pattern = (*failed_pattern) + 1;
    test_result = sector_erase_nvr_and_check(&flash, &num_of_failed_bits_in_nvr_pattern);

    if (num_of_failed_bits_in_nvr_pattern > 0)
    {
#ifdef QUICK_VERBOSE_TEST
        printf("Test failed\n");
#endif
        *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
        *gpio_output_value_32_63 = MRAM_TEST_FAILED;
        return -1;
    }
    num_of_failed_bits_in_nvr_pattern = 0;










#ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────────┐ \n"
           " │ W/R iCkbd w/ ECC  │ \n"
           " └───────────────────┘ \n");
#endif

    *failed_pattern = (*failed_pattern) + 1;

    num_of_failed_bits_in_mram_pattern = 0;
    num_of_failed_bits_in_nvr_pattern = 0;

    checkerboard = 0x55;
    inverse_checkerboard = 0xAA;
    pattern_to_use = 0x0;
    for (int mram_word = 0; mram_word < (BUFF_SIZE / BYTES_PER_ENTRY); mram_word++)
    {
        if (mram_word % 2 == 0)
        {
            pattern_to_use = inverse_checkerboard;
        }
        else
        {
            pattern_to_use = checkerboard;
        }
        for (int mram_word_byte = 0; mram_word_byte < BYTES_PER_ENTRY; mram_word_byte++)
        {
            tx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] = pattern_to_use;
            rx_buffer[(mram_word * BYTES_PER_ENTRY) + mram_word_byte] = 0;
        }
    }

    flash_addr = 0x0;
    for (int j = 0; j < NB_ITER; j++)
    {
        pi_flash_program(&flash, flash_addr, tx_buffer, BUFF_SIZE);
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 1);
        if (test_failed)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("Found at least 1 bit failing with ECC on. Test failed\n");
#endif
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }

        flash_addr += BUFF_SIZE;
    }

    pi_nvr_access_open(&flash);

    *failed_pattern = (*failed_pattern) + 1;

    for (int nvr_addr = 0; nvr_addr <= 0x100000; nvr_addr = nvr_addr + 0x100000)
    {
        // No more than one failed bits allowed
        int number_of_failed_bits_in_word = 0;

        pi_flash_program(&flash, nvr_addr, tx_buffer, NVR_SECTOR_SIZE_IN_BYTES);
        pi_flash_read(&flash, nvr_addr, rx_buffer, NVR_SECTOR_SIZE_IN_BYTES);

        for (int nvr_word = 0; nvr_word < (NVR_SECTOR_SIZE_IN_BYTES / BYTES_PER_ENTRY); nvr_word++)
        {
            number_of_failed_bits_in_word = 0;
            for (int nvr_word_byte = 0; nvr_word_byte < BYTES_PER_ENTRY; nvr_word_byte++)
            {
                if (rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte] != tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte])
                {
                    number_of_failed_bits_in_word = number_of_failed_bits_in_word + find_number_of_failed_bits(rx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte], tx_buffer[(nvr_word * BYTES_PER_ENTRY) + nvr_word_byte]);
                }
            }
            num_of_failed_bits_in_nvr_pattern += number_of_failed_bits_in_word;
            if (num_of_failed_bits_in_nvr_pattern > 0)
            {
                #ifdef QUICK_VERBOSE_TEST
                printf("More than 1 failures in a single row\n");
                #endif
                *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
                *gpio_output_value_32_63 = MRAM_TEST_FAILED;
                return -1;
            }
        }
    }
    pi_nvr_access_close(&flash);








#ifdef QUICK_VERBOSE_TEST
    printf(" ┌───────────────────┐ \n"
           " │ Chip Erase w/ ECC │ \n"
           " │      (Array)      │ \n"
           " └───────────────────┘ \n");
#endif

    *failed_pattern = (*failed_pattern) + 1;

    num_of_failed_bits_in_mram_pattern = 0;
    for (int i = 0; i < BUFF_SIZE; i++)
    {
        tx_buffer[i] = 0xFF;
        rx_buffer[i] = 0;
    }
    flash_addr = 0;
#ifdef QUICK_VERBOSE_TEST
    pi_flash_erase(&flash, 0, 16); // Temporary fix to delete one sector after
    pi_flash_erase_sector(&flash, 0);
    pi_flash_erase_sector(&flash, 8192);
#else
    pi_flash_erase_chip(&flash);
#endif
    for (int j = 0; j < NB_ITER; j++)
    {
        pi_flash_read(&flash, flash_addr, rx_buffer, BUFF_SIZE);

        test_failed = check_results(rx_buffer, tx_buffer, &num_of_failed_bits_in_mram_pattern, 1);
        if (test_failed)
        {
#ifdef QUICK_VERBOSE_TEST
            printf("Found at least 1 bit failing with ECC on. Test failed\n");
#endif
            *pattern_failed_bit_count = num_of_failed_bits_in_mram_pattern;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }
        flash_addr += BUFF_SIZE;
    }









#ifdef QUICK_VERBOSE_TEST
    printf(" ┌──────────────────┐ \n"
           " │ Sector Erase NVR │ \n"
           " │      w/ ECC      │ \n"
           " └──────────────────┘ \n");
#endif

    *failed_pattern = (*failed_pattern) + 1;

    num_of_failed_bits_in_nvr_pattern = 0;
    test_result = 0;
    sector_erase_nvr_and_check(&flash, &num_of_failed_bits_in_nvr_pattern);

    if (num_of_failed_bits_in_nvr_pattern > 0)
    {
#ifdef QUICK_VERBOSE_TEST
            printf("Found at least 1 bit failing with ECC on. Test failed\n");
#endif
        *pattern_failed_bit_count = num_of_failed_bits_in_nvr_pattern;
        *gpio_output_value_32_63 = MRAM_TEST_FAILED;
        return -1;
    }
    num_of_failed_bits_in_nvr_pattern = 0;





    #ifdef QUICK_VERBOSE_TEST
    printf(" ┌──────────────┐ \n"
           " │ W/R Trim NVR │ \n"
           " │    w/ ECC    │ \n"
           " └──────────────┘ \n");
    #endif

    *failed_pattern = (*failed_pattern) + 1;

    for (int i = 0; i < NVR_SECTOR_SIZE_IN_BYTES; i++)
    {
        tx_buffer[i] = 0xFF;
        rx_buffer[i] = 0;
    }
    flash_addr = 0;
    tx_buffer[0] = SA_trim_125C;
    tx_buffer[16] = log_FBC & 0x000000FF;
    tx_buffer[17] = (log_FBC >> 8) & 0x000000FF;
    tx_buffer[18] = (log_FBC >> 16) & 0x000000FF;
    tx_buffer[19] = (log_FBC >> 24) & 0x000000FF;
    // (unsigned char) (i>>8);
    pi_nvr_access_open(&flash);
    pi_flash_program(&flash, flash_addr, tx_buffer, 2*BYTES_PER_ENTRY);
    pi_flash_read(&flash, flash_addr, rx_buffer, 2*BYTES_PER_ENTRY);
    for (int i = 0; i < 2*BYTES_PER_ENTRY; i++)
    {
        //printf("Index %d: got %x expected %x\n", i, rx_buffer[i], tx_buffer[i]);

        if (rx_buffer[i] != tx_buffer[i])
        {
            #ifdef QUICK_VERBOSE_TEST
            printf("Error at index %d, expected 0x%2.2x, got 0x%2.2x\n", i, (unsigned char)i, rx_buffer[i]);
            printf("TEST FAILURE\n");
            #endif
            *pattern_failed_bit_count = 1;
            *gpio_output_value_32_63 = MRAM_TEST_FAILED;
            return -1;
        }
    }
    pi_nvr_access_close(&flash);

    pi_flash_close(&flash);

#ifdef QUICK_VERBOSE_TEST
    printf("TEST SUCCESS\n");
#endif

    *pattern_failed_bit_count = 0;
    *gpio_output_value_32_63 = MRAM_TEST_PASSED;

    // OPEN LOOP PAD TEST
    #ifdef QUICK_VERBOSE_TEST
    printf("Reset all FLL registers to default values\n");    
    #endif


    (*(uint32_t *)(0x1A100000 + 12)) = 0x10030A71;   //RESET F0CR1
    (*(uint32_t *)(0x1A100000 + 16)) = 0x000F0001;   //RESET F0CR2
    (*(uint32_t *)(0x1A100000 + 44)) = 0x00000202;   //RESET CCR1
    (*(uint32_t *)(0x1A100000 + 48)) = 0x000F0011;   //RESET CCR2


    (*(uint32_t *)(0x1A100000 + 4)) &= 0xCFFFFFFF;   //READOUT DCO CODE FROM FSR
    int DCO_CODE = (*(uint32_t *)(0x1A100000 + 0))>>16;
    #ifdef QUICK_VERBOSE_TEST
    printf("DCO INPUT CODE: 0x%x\n", DCO_CODE);
    #endif

    pi_pad_function_set(PI_PAD_067, PI_PAD_FUNC0);

    int32_t errors = 0;

    struct pi_pwm_ioctl_ch_config ch_conf[NB_PWM_USED] = {0};
    struct pi_pwm_ioctl_evt evt[NB_PWM_USED] = {0};

    pi_task_t cb_task[NB_PWM_USED] = {0};

    for (uint32_t i = 0; i < (uint32_t) NB_PWM_USED; i++)
    {
        pi_pwm_conf_init(&conf[i]);
        conf[i].pwm_id = pwm_configurations[i][0];
        conf[i].ch_id = pwm_configurations[i][1];
        pi_open_from_conf(&pwm[i], &conf[i]);
        errors = pi_pwm_open(&pwm[i]);
        if (errors)
        {
            #ifdef QUICK_VERBOSE_TEST
            printf("Error opening PWM(%ld) : %ld!\n", i, errors);
            #endif
            pmsis_exit(-1 - i);
        }

        (*(uint32_t *)(0x1A105000+8)) = 0x00030000;
        (*(uint32_t *)(0x1A105000+12)) = 0x00030001;    

        /* Setup output event. */
        evt[i].evt_sel = PI_PWM_EVENT_SEL0 + i;
        evt[i].evt_output = PI_PWM_EVENT_OUTPUT(i, (PI_PWM_CHANNEL0 + i));
        pi_pwm_ioctl(&pwm[i], PI_PWM_EVENT_SET, (void *) &evt[i]);
    }

    /* Start PWM timer. */
    for (uint32_t i = 0; i < (uint32_t) NB_PWM_USED; i++)
    {
        pi_pwm_timer_start(&pwm[i]);
        #ifdef QUICK_VERBOSE_TEST
        printf("Start PWM timer %ld !\n", i);
        #endif 
    }

    #ifdef QUICK_VERBOSE_TEST
    printf("PWM0 ON GPIO67 OUTPUTS FLL0 AT BOOT VALUES FREQ. DIVIDED BY 8.\n");
    printf("MIN FREQ FOR ABB BOOT OK: 4.625 MHz.\n");
    printf("MIN FREQ (TEMP <40C) FOR ABB BOOT OK w/ JITTER AND PVT TOLERANCE: 5.08 MHz.\n");
    #endif
    // DCO OPEN LOOP


    return 0;
}
