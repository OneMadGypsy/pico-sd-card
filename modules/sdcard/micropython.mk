SDCARD_MOD_DIR := $(USERMOD_DIR)
SRC_USERMOD += $(SDCARD_MOD_DIR)/sdcard.c
CFLAGS_USERMOD += -I$(SDCARD_MOD_DIR)