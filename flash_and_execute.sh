#!/bin/bash

path=`pwd`
echo "Set path to $path"

BINARY=$1
EXEC_ADDRESS=$2


if [ -z "$BINARY" ]
  then
    echo "No argument supplied, you should at least supply the path of the binary to be flashed"
    echo "Usage ./test.sh PATH_OF_BINARY [START_FUNCTION_ADDRESS]"
    exit 1
fi

if [ -f "${BINARY}" ]
then echo "${BINARY} binary to be flashed";
else echo "${BINARY} is not a valid file";
     exit 1
fi



FILESIZE=$(stat -c%s "$BINARY")
echo "Size of $BINARY = $FILESIZE bytes."

./openocd_ubuntu2204/bin/openocd -c "gdb_port disabled; telnet_port disabled; tcl_port disabled" -f "$path/openocd_tools/tcl/gapuino_ftdi.cfg" -f "$path/openocd_tools/tcl/gap9revb.tcl" -f "$path/openocd_tools/tcl/flash_image.tcl" -c "gap9_flash_raw ${BINARY:P} $FILESIZE $path/openocd_tools/gap_bins/gap_flasher-gap9_evk-mram.elf 0x2000; exit;"


if [ -z "$EXEC_ADDRESS" ]
  then
    echo "If you want to execute the binary you have to provide the address of the _start function in hexadecimal format (e.g. 0x1c010150)"
    echo "Usage ./test.sh PATH_OF_BINARY [START_FUNCTION_ADDRESS]"
    echo "You can use this command to get the _start function entry address: riscv32-unknown-elf-objdump multi_spi --source | grep \<_start\>"
    exit 0
fi

## Should the first argument be -d0 or -d2? 

./openocd_ubuntu2204/bin/openocd -d0 -c "gdb_port disabled; telnet_port disabled; tcl_port disabled" -f "$path/openocd_tools/tcl/gapuino_ftdi.cfg" -f "$path/openocd_tools/tcl/gap9revb.tcl" -c "load_and_start_binary  ${BINARY:P} $EXEC_ADDRESS"
