#!/bin/bash

# More safety, by turning some bugs into errors.
set -o errexit -o pipefail -o noclobber -o nounset

# ignore errexit with `&& true`
getopt --test > /dev/null && true
if [[ $? -ne 4 ]]; then
    echo 'I’m sorry, `getopt --test` failed in this environment.'
    exit 1
fi

path=`pwd`
echo "Set path to $path"


help()
{
    echo "Usage: ./flash_and_execute [ -m | --mram_img mram_img_file ]
                           [ -f | --flash_img flash_img_file ]
                           [ -e | --exec elf_file -a | --addr 0x1c0XXXXX ]
                           [ -h | --help  ]"
    exit 2
}

function getopts-extra () {
    declare i=1
    # if the next argument is not an option, then append it to array OPTARG
    while [[ ${OPTIND} -le $# && ${!OPTIND:0:1} != '-' ]]; do
        OPTARG[i]=${!OPTIND}
        let i++ OPTIND++
    done
}


# option --output/-o requires 1 argument
LONGOPTS=mram_img:,flash_img:,exec:,addr:,help
OPTIONS=m:,f:,e:,a:,h

# -temporarily store output to be able to check for errors
# -activate quoting/enhanced mode (e.g. by writing out “--options”)
# -pass arguments only via   -- "$@"   to separate them correctly
# -if getopt fails, it complains itself to stdout
PARSED=$(getopt -a -n test_new_flasher --options $OPTIONS --longoptions $LONGOPTS -- "$@")

VALID_ARGUMENTS=$# # Returns the count of arguments that are in short or long options

if [ "$VALID_ARGUMENTS" -eq 0 ]; then
  help
fi

# read getopt’s output this way to handle the quoting right:
eval set -- "$PARSED"


m=n f=n e=n addr=n
# now enjoy the options in order and nicely split until we see --
while true; do
    case "$1" in
        -m|--mram_img)
            m=$2
            shift 2
            ;;
        -f|--flash_img)
            f=$2
            shift 2
            ;;
        -e|--exec)
            e=$2
            shift 2
            ;;
        -a|--addr)
            addr=$2
            shift 2
            ;;
        -h | --help)
            help
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "Unexpected option: $1"
            help
            ;;
    esac
done

#handle non-option arguments
# if [[ $# -ne 1 ]]; then
#     echo "$0: A single input file is required."
#     exit 4
# fi

#echo "exec:  $e $addr, flash_img: $f, mram_img: $m"


## Flash INTO MRAM
if [[ "$m" != "n" ]] && [ -f $m ]
then
  FILESIZE=$(stat -c%s "$m")
  # The content of $m is different from "n" and is a file, then get the size and flash it
  printf "\n\nFlashing into MRAM $m of size $FILESIZE at defulat Address 0x2000\n\n"

  ./openocd_ubuntu2204/bin/openocd -c "gdb_port disabled; telnet_port disabled; tcl_port disabled" -f "$path/openocd_tools/tcl/gapuino_ftdi.cfg" -f "$path/openocd_tools/tcl/gap9revb.tcl" -f "$path/openocd_tools/tcl/flash_image.tcl" -c "gap9_flash_raw ${m} $FILESIZE $path/openocd_tools/gap_bins/gap_flasher-gap9_evk-mram.elf 0x2000; exit;"

fi

## Flash INTO OCTOSPI FLash
if [[ "$f" != "n" ]] && [ -f $f ]
then
  FILESIZE=$(stat -c%s "$f")
  # The content of $f is different from "n" and is a file, then get the size and flash it
  printf "\n\nFlashing into OCTOSPI Flash $f of size $FILESIZE at defulat Address 0x2000\n\n"

  ./openocd_ubuntu2204/bin/openocd -c "gdb_port disabled; telnet_port disabled; tcl_port disabled" -f "$path/openocd_tools/tcl/gapuino_ftdi.cfg" -f "$path/openocd_tools/tcl/gap9revb.tcl" -f "$path/openocd_tools/tcl/flash_image.tcl" -c "gap9_flash_raw ${f} $FILESIZE $path/openocd_tools/gap_bins/gap_flasher-gap9_evk.elf 0x2000; exit;"

fi

## Execute app from JTAG
if [[ "$e" != "n" ]] && [ -f $e ] && [[ "$addr" != "n" ]]
then
  # The content of $f is different from "n" and is a file, then get the size and flash it
  printf "\n\nExecuting ELF $e with START address at $addr\n\n"

  ./openocd_ubuntu2204/bin/openocd -d0 -c "gdb_port disabled; telnet_port disabled; tcl_port disabled" -f "$path/openocd_tools/tcl/gapuino_ftdi.cfg" -f "$path/openocd_tools/tcl/gap9revb.tcl" -c "load_and_start_binary  $e $addr"

fi
