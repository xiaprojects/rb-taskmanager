mkdir build && cd build
cmake ..
make -j$(nproc)
# Run inside your Wayfire session:
./wayfire-layout-switcher &
