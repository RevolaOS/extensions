# SPDX-License-Identifier: GPL-3.0-only
ID := chapters
MODULES := main
main_SOURCES := main.c chapters.c
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/rv.mk

.PHONY: check
check:
	python3 tests/run.py $(SDK)
