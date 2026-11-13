# SPDX-License-Identifier: GPL-3.0-only
ID := file-info
MODULES := main
main_SOURCES := main.c info.c
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/extension/rv.mk

.PHONY: check
check:
	python3 tests/run.py $(SDK)
