# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
# Full implementation: thin wrapper around CMake (CMakeLists.txt is the source of truth).
# Build directory is always ./build.

BUILD_TYPE ?= Release
PREFIX     ?= /opt/rb-taskmanager
DESTDIR    ?=

all:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(PREFIX)
	cmake --build build -j$$(nproc)

install: all
	DESTDIR=$(DESTDIR) cmake --install build --strip

clean:
	rm -rf ./build

dependencies:
	apt install -y g++ cmake pkg-config libgtk-3-dev libgtk-layer-shell-dev

.PHONY: all install clean dependencies
