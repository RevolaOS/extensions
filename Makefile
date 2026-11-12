# SPDX-License-Identifier: GPL-3.0-only
# Builds every extension into dist/<id>.rvx, and runs the host tests that exist.
#
#   make SDK=/path/to/revola-sdk            # all extensions
#   make check SDK=/path/to/revola-sdk      # host tests
#   make clean

ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif

EXTENSIONS := $(patsubst %/manifest.toml,%,$(wildcard */manifest.toml))
TESTED := $(patsubst %/tests,%,$(wildcard */tests))
DIST := $(abspath dist)

all: $(EXTENSIONS)

$(EXTENSIONS):
	$(MAKE) -C $@ SDK=$(abspath $(SDK)) OUT=$(DIST)/$@ -s
	cp $(DIST)/$@/$@.rvx $(DIST)/$@.rvx

check:
	for extension in $(TESTED); do $(MAKE) -C $$extension check SDK=$(abspath $(SDK)) || exit 1; done

clean:
	rm -rf $(DIST) $(addsuffix /build,$(EXTENSIONS))

.PHONY: all check clean $(EXTENSIONS)
