cmake_minimum_required(VERSION 3.16)
project(wayfire-layout-switcher)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(GDK3 REQUIRED gdk-3.0)
pkg_check_modules(GTK_LAYER_SHELL REQUIRED gtk-layer-shell-0)

include_directories(
    ${GTK3_INCLUDE_DIRS}
    ${GTK_LAYER_SHELL_INCLUDE_DIRS}
)

link_directories(
    ${GTK3_LIBRARY_DIRS}
    ${GTK_LAYER_SHELL_LIBRARY_DIRS}
)

add_definitions(
    ${GTK3_CFLAGS_OTHER}
    ${GTK_LAYER_SHELL_CFLAGS_OTHER}
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(wayfire-layout-switcher
    wayfire-layout-switcher.cpp
    wayfire_ipc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/rb_http.cpp
)
target_compile_options(wayfire-layout-switcher PRIVATE -Wall -Wextra)

target_link_libraries(wayfire-layout-switcher
    ${GTK3_LIBRARIES}
    ${GTK_LAYER_SHELL_LIBRARIES}
    pthread
)

# ── Install layout (used by `make install` and the rb-taskmanager .deb) ────────
# Binaries and assets go under CMAKE_INSTALL_PREFIX (default /opt/rb-taskmanager).
# The config file always goes to /etc/rb-taskmanager. DESTDIR is honoured.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/opt/rb-taskmanager" CACHE PATH "Install prefix" FORCE)
endif()

install(TARGETS wayfire-layout-switcher RUNTIME DESTINATION bin)
install(PROGRAMS
            packaging/rb-layout-switcher
            packaging/rb-launch-chromium
        DESTINATION bin)
install(FILES index.html DESTINATION switcher-www)
install(FILES packaging/layout-switcher.conf DESTINATION /etc/rb-taskmanager)
