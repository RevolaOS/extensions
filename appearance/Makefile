# SPDX-License-Identifier: GPL-3.0-only
ID := appearance
MODULES := main switch
main_SOURCES := main.c look.c style.c render.c catalog.c crc.c tint.c selection_bar.c \
    menu_covers.c flat_cover.c latch.c theme.c theme_picker.c lz.c assets.c
switch_SOURCES := switch.c pack.c derive.c stock.c
OUT ?= build
# The Classic dark artwork, compiled from the themes repository's source when
# THEMES names a checkout; a build without one keeps the file it has. It stays
# under build/ whatever OUT is, where manifest.toml names it.
CLASSIC := build/classic.rvt
ASSETS := $(wildcard assets/*.png) $(CLASSIC)
EXTRA_CFLAGS := -I$(OUT)
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/rv.mk

$(OUT)/catalog.inc: catalog.json tools/catalog_inc.py | $(OUT)
	python3 tools/catalog_inc.py $< $@
$(OUT)/main/catalog.c.o: $(OUT)/catalog.inc

# rvt.h and catalog.json are the themes repository's, at the commit in
# themes.lock. With THEMES set, the build checks the copies against the
# checkout and the checkout against the lock, once the lock names a commit;
# `make vendor` takes the checkout's files and `make lock` records its commit.
ifdef THEMES
RVT_SOURCE := $(THEMES)/rvt/data/rvt.h
CATALOG_SOURCE := $(THEMES)/rvt/data/osos-2.0.5.json
LOCKED := $(shell sed -n 's/^commit = "\(.*\)"/\1/p' themes.lock)
$(OUT)/vendored: rvt.h catalog.json themes.lock $(RVT_SOURCE) $(CATALOG_SOURCE) | $(OUT)
	cmp rvt.h $(RVT_SOURCE)
	cmp catalog.json $(CATALOG_SOURCE)
ifneq ($(LOCKED),)
	test "$$(git -C $(THEMES) rev-parse HEAD)" = "$(LOCKED)"
endif
	touch $@
$(OUT)/$(ID).rvx: $(OUT)/vendored

vendor:
	cp $(RVT_SOURCE) rvt.h
	cp $(CATALOG_SOURCE) catalog.json

lock:
	printf '%s\n' \
	    '# The themes repository commit that rvt.h, catalog.json and build/classic.rvt' \
	    '# come from: `make vendor THEMES=...` copies the first two, `make lock` writes this.' \
	    "commit = \"$$(git -C $(THEMES) rev-parse HEAD)\"" > themes.lock

$(CLASSIC): $(THEMES)/themes/classic/theme.toml $(wildcard $(THEMES)/themes/classic/dark/*) \
    $(wildcard $(THEMES)/rvt/*.py) $(RVT_SOURCE) catalog.json tools/catalog_inc.py
	mkdir -p build
	cd $(THEMES) && python3 -m rvt compile themes/classic --allow-reserved \
	    --out $(abspath build)/classic-build >/dev/null
	python3 tools/catalog_inc.py catalog.json --check build/classic-build/classic/classic.rvt
	cp build/classic-build/classic/classic.rvt $@
else
$(CLASSIC):
	$(error $@ is missing: set THEMES to a themes checkout to build the Classic dark artwork)
endif

.PHONY: check vendor lock
check:
	python3 tests/run.py $(SDK)
