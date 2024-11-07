# Gap Utils

This repository provides essential utilities for flashing and executing a GAP9 binary on a board.

## Getting Started

Few test binaries are included to verify that flashing and running works correctly on a GAP9 EVK. To flash and execute a "blink LED" example, you can use the following command:

```bash
./flash_and_execute.sh test_elf/blink_led.elf 0x1c010150
```


## Usage 


Usage: `./flash_and_execute.sh BINARY_FILE [_START_ADDRESS_HEX]`

- `BINARY_FILE`: is the relative or absolute path to the binary file to be flashed onto GAP9.

- `_START_ADDRESS_HEX` (optional): The hexadecimal address of the _start function (the entry point of the executable). If provided, this address will be used to execute the binary through JTAG.
To find the correct start address, you can use the RISC-V GCC toolchain with the following command:

- is an optional argument containing the hexadecimal address of the function _start (the entry point of the executable).If it is present it will use the entry address to execute the binary through jtag. To found the correct address you can use riscv32 gcc tolchain with the following command: `riscv32-unknown-elf-objdump multi_spi --source | grep \<_start\>"`

The riscv32 gcc toolchain can be found [here](https://github.com/GreenWaves-Technologies/gap_gnu_toolchain)


## Limitations

For the moment only Ubuntu 22.04 is supported. We plan to support also windows 11. 
