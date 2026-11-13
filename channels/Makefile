# SPDX-License-Identifier: GPL-3.0-only
ID := channels
MODULES := main editor
main_SOURCES := main.c channels.c modes.c
editor_SOURCES := editor.c labels.c modes.c
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/extension/rv.mk

.PHONY: check
check:
	python3 tests/run.py $(SDK)
