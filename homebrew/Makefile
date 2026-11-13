# SPDX-License-Identifier: GPL-3.0-only
# make SDK=/path/to/revola-sdk
ID := homebrew
MODULES := main
main_SOURCES := main.c
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/extension/rv.mk
