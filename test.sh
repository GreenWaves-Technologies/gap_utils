path=`pwd`
echo $path

#./openocd_ubuntu2204/bin/openocd -d2 -c "gdb_port disabled; telnet_port disabled; tcl_port disabled" -f "$path/openocd_tools/tcl/gapuino_ftdi.cfg" -f "$path/openocd_tools/tcl/gap9revb.tcl" -c "load_and_start_binary /home/yao/projects/sdk/gap_sdk/examples/gap9/basic/helloworld/build/helloworld 0x1c010160"

./openocd_ubuntu2204/bin/openocd -c "gdb_port disabled; telnet_port 13826; tcl_port disabled" -f "$path/openocd_tools/tcl/gapuino_ftdi.cfg" -f "$path/openocd_tools/tcl/gap9revb.tcl" -f "$path/openocd_tools/tcl/flash_image.tcl" -c "$path/test_elf/blink_led_mram.bin 60064 $path/openocd_tools/gap_bins/gap_flasher-gap9_evk-mram.elf 0x2000; exit;"
