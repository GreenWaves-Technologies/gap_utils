-include $(RULES_DIR)/properties.mk

#override config_args += --config-opt=**/rtl/args=+EXEC_TEST=READ_MRAM_DIAGNOSTICS


APP = test
APP_SRCS = test.c 
APP_SRCS += fuser/gap_fuser.c 
APP_CFLAGS += -Os -g 
APP_CFLAGS += -Ifuser 

RUN_125C_TEST ?= 1
FUSE_TRIM_VAL ?= 0  # by default it won't fuse the trim value

ifeq ($(RUN_125C_TEST), 1)
	APP_CFLAGS += -DSENSE_AMP_ALGO_125C
endif

ifeq ($(FUSE_TRIM_VAL), 1)
	APP_CFLAGS += -DFUSE_TRIM_VALUE
endif


include $(RULES_DIR)/pmsis_rules.mk
