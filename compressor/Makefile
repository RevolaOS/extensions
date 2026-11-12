# SPDX-License-Identifier: GPL-3.0-only
ID := compressor
MODULES := main editor
main_SOURCES := main.c curve.c design.c
# Every frame is about nine 32 by 32 to 64 bit multiplies: one smull each in
# ARM, a library call each in Thumb.
main_ARM_SOURCES := compressor.c
editor_SOURCES := editor.c labels.c curve.c
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/rv.mk

.PHONY: check
check:
	python3 tests/run.py $(SDK)
