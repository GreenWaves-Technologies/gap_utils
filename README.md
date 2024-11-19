# Gap Utils

This repository provides essential utilities for flashing and executing a GAP9 application on a board. 

The GAP9 build system generates three files:
- An ELF with the code of the application that can be executed from MRAM or form JTAG 
- An MRAM image which, depending on the memory layout, contains code and optionally data partitions
- An optional OCTOSPI Flash Image which usually contains only data, but could also contain code, since GAP9 could also boot from OCTOSPI Flash


These three files are generated in the build folder of the application:

- The Application ELF has the application name which is defined in the CMakeLists.txt under the variable TARGET_NAME.
- mram.bin_0 
- flash.bin_0 

For example for the blink led example the build folder will contain:


## Getting Started

Few test binaries are included to verify that flashing and running works correctly on a GAP9 EVK. 


### Blink LED 
To flash the "blink LED" example, you can use the following command:

```bash
./flash_and_execute.sh --mram_img test_elf/blink_led_mream.bin 
```
To test is you need to shortcut the jumper BOOT1 (check naming behind the EVK) and reset the board with the reset button. If the LED blinks the flash process has run successfully.

### Hello World

This tool also permits to execute code from JTAG without the need of flash anything into the board, to do so you can use the following options:

```bash
./flash_and_execute.sh --exec test_elf/helloworld --addr 0x1c010150
```
You have to provide the ELF and the address of the _start function. To find this address there is command provided in the following section. 

### Mobilenet

In case of multiple flash usage and need to debug printf on jtag this example shows how to flash the two images to MRAM and OCTOSPI Flash and execute the elf code from jtag:

```bash
./flash_and_execute.sh --mram_img test_elf/mobilenet_mram.bin_0 --flash_img test_elf/mobilenet_flash.bin_0  --exec test_elf/mobilenet --addr 0x1c0101e0
```

In this example, the NN paramters are copied into MRAM and OCTOSPI flash along with all other images partitions. These are then used from the ELF file to execute the application. 
Along with the ELF provided using the --exec argument, you also need to also specify the address of the `_start` function (Instructions for finding this address are provided in the following section). The execution process will load an image from JTAG and run Mobilenet, displaying the detected class and per-layer performance metrics.

This application cannot run from MRAM since the printf included will avoid the application to run. They must be redirected to UART or None to be used. 

## Usage 

```
Usage: ./flash_and_execute [ -m | --mram_img mram_img_file ]
                           [ -f | --flash_img flash_img_file ]
                           [ -e | --exec elf_file -a | --addr 0x1c0XXXXX ]
                           [ -h | --help  ]
```

- `-m|--mram_img mram_img_file`: is the relative or absolute path to the EMRAM image to be flashed onto GAP9.

- `-f|--flash_img flash_img_file`: is the relative or absolute path to the OCTOSPI image to be flashed.

- `-e|--exec elf_file -a|--addr 0x1c0XXXXX ` elf_file is the path of the elf to executed through JTAG and 0x1c0XXXXX is the hexadecimal address of the function _start (the entry point of the executable). Both arguments must be provided to execute the ELF through JTAG. To found the correct address you can use riscv32 gcc tolchain with the following command: `riscv32-unknown-elf-objdump multi_spi --source | grep \<_start\>"`

The riscv32 gcc toolchain can be found [here](https://github.com/GreenWaves-Technologies/gap_gnu_toolchain)


## Known Limitations

- Only Ubuntu is supported (Tested on 22.04). Next releases will also support windows 11. 
- This tool can only flash one image per flash type, next releases will be able to handle multiple images per flash with different flash addresses.
- Only digilent like ftdi is supported.
