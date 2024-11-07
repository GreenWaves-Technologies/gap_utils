
::step1
::note: *** VQPS MUST keep low in step1 ***
.\openocd.exe -d0 -c "gdb_port disabled; telnet_port disabled; tcl_port disabled" -f "openocd_tools/tcl/olimex-arm-usb-ocd-h.cfg" -f "openocd_tools/tcl/gap9revb.tcl" -c "load_and_start_binary gap_fuser_step1 0x1c010150"

::step2
::note: *** VQPS MUST power up before run step2 ***
.\openocd.exe -d0 -c "gdb_port disabled; telnet_port disabled; tcl_port disabled" -f "openocd_tools/tcl/olimex-arm-usb-ocd-h.cfg" -f "openocd_tools/tcl/gap9revb_no_reset.tcl" -c "load_and_start_binary gap_fuser_step2 0x1c010150"

::step3
::note: *** VQPS MUST keep low in step3 ***
.\openocd.exe -d0 -c "gdb_port disabled; telnet_port disabled; tcl_port disabled" -f "openocd_tools/tcl/olimex-arm-usb-ocd-h.cfg" -f "openocd_tools/tcl/gap9revb_no_reset.tcl" -c "load_and_start_binary gap_fuser_step3 0x1c010150"
