source [find tcl/gap9revb_common.tcl]
adapter_khz     5000

config_reset 0x1

target create $_FC riscv -chain-position $_TAP_RISCV -coreid 0x9 
#target create $_CL8 riscv -chain-position $_TAP_RISCV -coreid 0x8 -defer-examine
#target smp $_FC $_CL8

add_cluster

create_smp

#$_CL8 configure -rtos hwthread
#$_FC configure -rtos hwthread


gdb_report_data_abort enable
gdb_report_register_access_error enable

riscv set_reset_timeout_sec 36000
riscv set_command_timeout_sec 36000

# prefer to use sba for system bus access
riscv set_prefer_sba on

proc jtag_init {} {
    puts "jtag init"
    targets $::_FC

    gap_reset 1 100
    disable_abb
    gap_reset 1 100
    
    # wait for jtag ready
    poll_confreg 0xb
    #poll_confreg 0x7
    echo "INIT: confreg polling done"

    ## examine_cluster

    #$::_CL8 arp_examine
    examine_cluster
    $::_FC arp_examine
    #$::_FC arp_halt
    halt_all
    $::_FC arm semihosting enable
    echo "INIT: examine done"
    jtag arp_init
}

proc init_reset {mode} {
    puts "hello"

    gap_reset 1 100

    # wait for jtag ready
    poll_confreg 0xb
    echo "RESET: confreg polling done"
    jtag arp_init
}

# dump jtag chain
#scan_chain

init


#targets $::_FC
#ftdi_set_signal nSRST 1
halt

#target smp $_FC $_CL8
#$::_FC arm semihosting enable

echo "Ready for Remote Connections"
