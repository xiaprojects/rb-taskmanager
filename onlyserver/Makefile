# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
# onlyserver: HTTP server for Waydroid <-> host interaction (no GTK, no Wayfire).

CXX      ?= g++
CXXFLAGS ?= -O2 -g
override CXXFLAGS += -std=c++17 -Wall -Wextra
LDLIBS   += -lpthread

PREFIX      ?= /opt/rb-taskmanager
SYSTEMD_DIR ?= /usr/lib/systemd/system
DESTDIR     ?=

TARGET := rb-taskmanager-onlyserver
SRCS   := rb-taskmanager-onlyserver.cpp ../common/rb_http.cpp
HDRS   := ../common/rb_http.hpp

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRCS) -o $@ $(LDFLAGS) $(LDLIBS)

# Staged/packaging install: touches only $(DESTDIR), no systemctl.
install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/www $(DESTDIR)$(SYSTEMD_DIR)
	install -m 0755 -s $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	install -m 0755 shutdown.sh mode.sh $(DESTDIR)$(PREFIX)/bin/
	install -m 0644 www/* $(DESTDIR)$(PREFIX)/www/
	install -m 0644 rb-taskmanager.service $(DESTDIR)$(SYSTEMD_DIR)/

# Direct install on this machine (needs root). Prefer the .deb: `make deb-onlyserver` at repo root.
install-onlyserver: install
	systemctl daemon-reload
	systemctl enable rb-taskmanager.service

clean:
	rm -f $(TARGET)

dependencies:
	apt install -y g++ make

.PHONY: all install install-onlyserver clean dependencies
