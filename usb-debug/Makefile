# SPDX-License-Identifier: GPL-3.0-only
# make SDK=/path/to/revola-sdk            the extension
# make SDK=/path/to/revola-sdk MAILBOX=1  emulator test build; never install it on a device
ID := usb-debug
MODULES := main
main_SOURCES := main.c
ifeq ($(MAILBOX),1)
EXTRA_CFLAGS += -DUSB_DEBUG_MAILBOX
endif
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/rv.mk
