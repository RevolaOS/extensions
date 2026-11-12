# SPDX-License-Identifier: GPL-3.0-only
ID := crossfeed
MODULES := main editor
main_SOURCES := main.c design.c
# Every sample is three or four 32 by 32 to 64 bit multiplies: one smull each
# in ARM, a library call each in Thumb.
main_ARM_SOURCES := crossfeed.c
editor_SOURCES := editor.c response.c labels.c
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/rv.mk

.PHONY: check
check:
	python3 tests/run.py $(SDK)
