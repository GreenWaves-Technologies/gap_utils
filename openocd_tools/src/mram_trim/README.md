# MRAM Trim Tool

This tool will allow you to test the GAP9 engineering samples in room tempreture, in order to get the trim value and fuse it into the efuse. 
This process will help you to make the GAP9 engineering samples working more stable with eMRAM.

## Trim Test w/o Fusing

No Special HW requirement

Run the script:

'''
source run_trim.sh 0
'''

This script will compile and run the test twice to find the correct trim value. 
In the end of this test, you will see:

'''
trim val to be fused XX
'''

This value should be between 0-50

## Trim Test w/ Fusing

HW requirement: To program the efuse, you should make sure that **PAD C9 (VQPS_FUSE_1V8)** is powered with 1V8
For example,
    On GAP9Mod, this is controlled by the jumper **J1**

Once the 1V8 is present, you just need to run the command:

'''
source run_trim.sh
'''
