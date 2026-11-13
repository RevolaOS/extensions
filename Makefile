# SPDX-License-Identifier: GPL-3.0-only
# Builds every extension into dist/<id>.rvx, and runs the host tests that exist.
#
#   make SDK=/path/to/revola-sdk THEMES=/path/to/themes   # all extensions
#   make check SDK=/path/to/revola-sdk                      # host tests
#   make clean
#
# THEMES names a checkout of the themes repository at the commit in
# appearance/themes.lock; only the appearance extension needs it.

ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif

EXTENSIONS := $(patsubst %/manifest.toml,%,$(wildcard */manifest.toml))
TESTED := $(patsubst %/tests,%,$(wildcard */tests))
DIST := $(abspath dist)

all: $(EXTENSIONS) $(DIST)/assets.json

$(EXTENSIONS):
	$(MAKE) -C $@ SDK=$(abspath $(SDK)) $(if $(THEMES),THEMES=$(abspath $(THEMES))) OUT=$(DIST)/$@ -s
	cp $(DIST)/$@/$@.rvx $(DIST)/$@.rvx

# The bitmap assets of the core and of every extension, one JSON record per
# line: what a theme must match to replace one. The themes repository vendors
# it as rvt/data/extensions.json.
$(DIST)/assets.json: $(EXTENSIONS)
	cat $(SDK)/extension/assets.json > $@
	for extension in $(EXTENSIONS); do \
		python3 $(SDK)/extension/assets.py $$extension/manifest.toml $(DIST)/$$extension.rvx >> $@ || exit 1; \
	done

check:
	for extension in $(TESTED); do $(MAKE) -C $$extension check SDK=$(abspath $(SDK)) || exit 1; done

clean:
	rm -rf $(DIST) $(addsuffix /build,$(EXTENSIONS))

.PHONY: all check clean $(EXTENSIONS)
