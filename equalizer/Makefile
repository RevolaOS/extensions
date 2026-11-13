# SPDX-License-Identifier: GPL-3.0-only
ID := equalizer
MODULES := main editor
main_SOURCES := main.c engine.c biquad.c presets.c
# The cascade multiplies 32 by 32 to 64 bits per coefficient and sample: one
# ARM instruction each, a library call each in Thumb.
main_ARM_SOURCES := cascade.c
editor_SOURCES := editor.c
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/extension/rv.mk

.PHONY: check
check:
	python3 tests/run.py $(SDK)
