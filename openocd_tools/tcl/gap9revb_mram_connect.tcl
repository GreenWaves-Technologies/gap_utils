source [find tcl/gap9revb_common.tcl]
adapter_khz     5000

config_reset 0x1

target create $_FC riscv -chain-position $_TAP_RISCV -coreid 0x9 

gdb_report_data_abort enable
gdb_report_register_access_error enable

riscv set_reset_timeout_sec 1440
riscv set_command_timeout_sec 1440

# prefer to use sba for system bus access
riscv set_prefer_sba on

proc jtag_init {} {
    puts "jtag init"
    targets $::_FC

    poll_confreg_noblock 0x7
    $::_FC arp_examine
    echo "examine done"
    jtag arp_init
}

proc init_reset {mode} {
    puts "reseting soc"
    #targets $::_FC
    # wait for jtag ready
    jtag arp_init
}

init

halt

$::_FC arm semihosting enable

echo "Ready for Remote Connections"
