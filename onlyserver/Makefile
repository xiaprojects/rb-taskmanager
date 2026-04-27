CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra

PKG_CONFIG := pkg-config

GTK3_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK3_LIBS := $(shell $(PKG_CONFIG) --libs gtk+-3.0)

GTK_LAYER_SHELL_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk-layer-shell-0)
GTK_LAYER_SHELL_LIBS := $(shell $(PKG_CONFIG) --libs gtk-layer-shell-0)

CXXFLAGS += $(GTK3_CFLAGS) $(GTK_LAYER_SHELL_CFLAGS)
LDFLAGS := $(GTK3_LIBS) $(GTK_LAYER_SHELL_LIBS) -lpthread

TARGET := rb-taskmanager
SRCS := main.cpp wayfire_ipc.cpp
OBJS := $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

rb-taskmanager-onlyserver:
	g++ rb-taskmanager-onlyserver.cpp -o rb-taskmanager-onlyserver

install-onlyserver: rb-taskmanager-onlyserver
	mkdir -p /opt/rb-taskmanager/bin
	cp -R www /opt/rb-taskmanager
	cp rb-taskmanager-onlyserver /opt/rb-taskmanager/bin
	cp shutdown.sh /opt/rb-taskmanager/bin
	chmod a+x /opt/rb-taskmanager/bin/shutdown.sh	
	cp rb-taskmanager.service /lib/systemd/system
	systemctl daemon-reload
	systemctl enable rb-taskmanager.service

clean:
	rm -f $(OBJS) $(TARGET)

dependencies:
	apt install -y libgtk-3-dev


.PHONY: all clean deb