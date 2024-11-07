APP              = fuser
APP_SRCS        += gap_fuser.c
APP_INC	        +=


APP_CFLAGS += -DJTAG_DISABLE
#APP_CFLAGS += -DDUMP_ONLY
MRAM_TRIM_VAL   ?= 35
APP_CFLAGS += -DMRAM_TRIM_VAL=$(MRAM_TRIM_VAL)


gen:
	./gap9-efuse-gen  --output=gap9_fuser_map.h --name=fuser_map --usecase=mram


include $(RULES_DIR)/pmsis_rules.mk
