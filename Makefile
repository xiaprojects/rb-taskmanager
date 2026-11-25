# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
# Top-level build: compiles both components and builds the two .deb packages into ./dist.
#
#   make deb                 # both packages
#   make deb-onlyserver      # rb-taskmanager-onlyserver_<ver>_<arch>.deb
#   make deb-switcher        # rb-taskmanager_<ver>_<arch>.deb (needs GTK3 + gtk-layer-shell dev)
#
# Build on the target architecture (e.g. on the RB-06 Pi itself) so library versions match.

VERSION := $(shell cat VERSION)

all:
	$(MAKE) -C onlyserver
	$(MAKE) -C rbtaskmanager

deb: deb-onlyserver deb-switcher

deb-onlyserver:
	rm -rf ./build/stage/rb-taskmanager-onlyserver
	$(MAKE) -C onlyserver install DESTDIR=$(CURDIR)/build/stage/rb-taskmanager-onlyserver
	packaging/mkdeb.sh rb-taskmanager-onlyserver build/stage/rb-taskmanager-onlyserver $(VERSION) dist

deb-switcher:
	rm -rf ./build/stage/rb-taskmanager
	$(MAKE) -C rbtaskmanager install DESTDIR=$(CURDIR)/build/stage/rb-taskmanager
	packaging/mkdeb.sh rb-taskmanager build/stage/rb-taskmanager $(VERSION) dist

dependencies:
	$(MAKE) -C onlyserver dependencies
	$(MAKE) -C rbtaskmanager dependencies

clean:
	$(MAKE) -C onlyserver clean
	$(MAKE) -C rbtaskmanager clean
	rm -rf ./build ./dist

.PHONY: all deb deb-onlyserver deb-switcher dependencies clean
