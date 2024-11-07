set _CHIPNAME gap9

jtag newtap $_CHIPNAME riscv -irlen 5 -expected-id 0x20020bcb
jtag newtap $_CHIPNAME pulp -irlen 4 -expected-id 0x20021bcb

foreach t [jtag names] {
	puts [format "TAP: %s\n" $t]
}


set _TAP_RISCV $_CHIPNAME.riscv
set _TAP_PULP $_CHIPNAME.pulp
set _CL0 $_CHIPNAME.cl0
set _CL1 $_CHIPNAME.cl1
set _CL2 $_CHIPNAME.cl2
set _CL3 $_CHIPNAME.cl3
set _CL4 $_CHIPNAME.cl4
set _CL5 $_CHIPNAME.cl5
set _CL6 $_CHIPNAME.cl6
set _CL7 $_CHIPNAME.cl7
set _CL8 $_CHIPNAME.cl8
set _FC  $_CHIPNAME.fc

proc config_reset { trst } {
    if { $trst == 1 } {
        reset_config srst_nogate trst_and_srst
    } else {
        reset_config srst_nogate srst_only
    }
}

proc add_cluster {} {
## Commented code, to create individual gdb connections per core
#target create $_CL8 riscv -chain-position $_TARGETNAME -coreid 0x8 -gdb-port 6666 -defer-examine
#target create $_CL8 riscv -chain-position $_TARGETNAME -coreid 0x8 -gdb-port 6666
    target create $::_CL0 riscv -chain-position $::_TAP_RISCV -coreid 0x0 -defer-examine
    target create $::_CL1 riscv -chain-position $::_TAP_RISCV -coreid 0x1 -defer-examine
    target create $::_CL2 riscv -chain-position $::_TAP_RISCV -coreid 0x2 -defer-examine
    target create $::_CL3 riscv -chain-position $::_TAP_RISCV -coreid 0x3 -defer-examine
    target create $::_CL4 riscv -chain-position $::_TAP_RISCV -coreid 0x4 -defer-examine
    target create $::_CL5 riscv -chain-position $::_TAP_RISCV -coreid 0x5 -defer-examine
    target create $::_CL6 riscv -chain-position $::_TAP_RISCV -coreid 0x6 -defer-examine
    target create $::_CL7 riscv -chain-position $::_TAP_RISCV -coreid 0x7 -defer-examine
    target create $::_CL8 riscv -chain-position $::_TAP_RISCV -coreid 0x8 -defer-examine
}

proc examine_cluster {} {
    $::_CL0 arp_examine
    $::_CL1 arp_examine
    $::_CL2 arp_examine
    $::_CL3 arp_examine
    $::_CL4 arp_examine
    $::_CL5 arp_examine
    $::_CL6 arp_examine
    $::_CL7 arp_examine
    $::_CL8 arp_examine
}

proc halt_all {} {
    $::_CL0 arp_halt
    $::_CL1 arp_halt
    $::_CL2 arp_halt
    $::_CL3 arp_halt
    $::_CL4 arp_halt
    $::_CL5 arp_halt
    $::_CL6 arp_halt
    $::_CL7 arp_halt
    $::_CL8 arp_halt
    $::_FC arp_halt
}

proc create_smp {} {
    target smp $::_FC $::_CL0 $::_CL1 $::_CL2 $::_CL3 $::_CL4 $::_CL5 $::_CL6 $::_CL7 $::_CL8
    $::_CL0 configure -rtos hwthread
    $::_CL1 configure -rtos hwthread
    $::_CL2 configure -rtos hwthread
    $::_CL3 configure -rtos hwthread
    $::_CL4 configure -rtos hwthread
    $::_CL5 configure -rtos hwthread
    $::_CL6 configure -rtos hwthread
    $::_CL7 configure -rtos hwthread
    $::_CL8 configure -rtos hwthread
    $::_FC configure -rtos hwthread
}

# First set bootmode.
# Then, poll confreg until end-of-boot (0x3) is set
proc poll_confreg { value } {
    irscan $::_TAP_PULP 0x6
    # size then value
    set ret [eval drscan $::_TAP_PULP 0x8 $value]
    puts "ret=$ret"
    set count [expr { 0x0 }]
    while { [expr {($ret != 0x3) && ($count < 100)}] } {
        irscan $::_TAP_PULP 0x6
        # size then value
        set ret [eval drscan $::_TAP_PULP 0x8 $value]
        #puts "ret=$ret"
        sleep 50
	set count [expr { $count + 0x1 }]
    }
	
    if { [expr {$count == 100}] } {
	puts "\[ERR\]:Jtag connect (ret = $ret)"
        exit
    }
    puts "\[OK\]:Jtag connect"	
}

# First set bootmode.
# Then, poll confreg only to the point of jtag enable
# Useful for boot from flash which does not set end of boot (0x3)
proc poll_confreg_noblock { value } {
    irscan $::_TAP_PULP 0x6
    # size then value
    set ret [eval drscan $::_TAP_PULP 0x8 $value]
    puts "ret=$ret"
    while { $ret != 0x3 } {
        irscan $::_TAP_PULP 0x6
        # size then value
        set ret [eval drscan $::_TAP_PULP 0x8 $value]
        puts "ret=$ret"
        sleep 50
        set ret 0x3
    }
}

proc gap_reset { trst reset_time } {
    jtag_reset $trst 1
    sleep $reset_time
    jtag_reset 0 0
    sleep $reset_time
}

proc disable_abb {} {
    irscan $::_TAP_PULP 0xA
    set ret1 [ eval drscan $::_TAP_PULP 0x20 0x017F1E7D]
    set ret2 [ eval drscan $::_TAP_PULP 0x20 0xDEAD5EA5]
    puts "ret1=$ret1"
    puts "ret2=$ret2"

    irscan $::_TAP_PULP 0xA
    set ret1 [ eval drscan $::_TAP_PULP 0x20 0x00030000]
    puts "ret1=$ret1"
}

proc enable_abb {} {
    irscan $::_TAP_PULP 0xA
    set ret1 [ eval drscan $::_TAP_PULP 0x20 0x00000000]
    puts "ret1=$ret1"
}

proc load_and_start_binary { elf_file pc_entry } {
    targets $::_FC
    # first ensure we are rest and halt so that pc is accessible
    #$::_FC arp_reset assert 1
    #reset halt
    halt
    load_image ${elf_file} 0x0 elf
    reg pc ${pc_entry}
    resume
}

#proc cluster_reset { addr } {
    # first reset the cluster

#    poll off
#    $::_FC mww 0x10200008 0x0
#    $::_FC mww 0x1a1040e4 0x200
    # SOC CTRL + 0x170
#    $::_FC mww 0x1a104170 0x0
#    sleep 1
#    $::_FC mww 0x1a104170 0x1

    # CLUSTER Ctrl: 0x10000000 + 0x00200000
    # addr: +0x40
#    $::_FC mww 0x10200040 $addr 9
    # fetch en: +0x8
#    $::_FC mww 0x10200008 0x3ff
    # available: + 0xe4
#    $::_FC mww 0x1a1040e4 0xffffffff
#    $::_CL0 arp_halt
#   $::_CL1 arp_halt
#   $::_CL2 arp_halt
#   $::_CL3 arp_halt
#   $::_CL4 arp_halt
#   $::_CL5 arp_halt
#   $::_CL6 arp_halt
#   $::_CL7 arp_halt
#   $::_CL8 arp_halt
#   $::_CL0 riscv set_ebreakm on
#   $::_CL1 riscv set_ebreakm on
#   $::_CL2 riscv set_ebreakm on
#   $::_CL3 riscv set_ebreakm on
#   $::_CL4 riscv set_ebreakm on
#   $::_CL5 riscv set_ebreakm on
#   $::_CL6 riscv set_ebreakm on
#   $::_CL7 riscv set_ebreakm on
#   $::_CL8 riscv set_ebreakm on
#   poll on
#}
