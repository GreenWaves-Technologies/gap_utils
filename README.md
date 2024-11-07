# Gap Utils


This repository contains the minimum utilities to be able to flash and execute a Gap9 binary on a board. 


## Getting started

Some test binary are provided within thie repository to test that flash and run works fine on a Gap9 EVK. To flash and run a blink led you can use the following commands:

```sh
./flash_and_execute.sh test_elf/blink_led.elf 0x1c010150
```

## Usage 


Usage: `./flash_and_execute.sh BINARY_FILE [_START_ADDRESS_HEX]`

- `BINARY_FILE`: is the relative or absolute path to the binary file to be flashed onto Gap9

- `_START_ADDRESS_HEX`: is an optional argument containing the hexadecimal address of the function _start (the entry point of the executable).If it is present it will use the entry address to execute the binary through jtag. To found the correct address you can use riscv32 gcc tolchain with the following command: riscv32-unknown-elf-objdump multi_spi --source | grep \<_start\>"


The riscv32 gcc toolchain can be found [here](https://github.com/GreenWaves-Technologies/gap_gnu_toolchain)
