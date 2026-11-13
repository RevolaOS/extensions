# SPDX-License-Identifier: GPL-3.0-only
# make SDK=/path/to/revola-sdk
ID := eu-volume-limit
MODULES := main
main_SOURCES := main.c
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/extension/rv.mk
