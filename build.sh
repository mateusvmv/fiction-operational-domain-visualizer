#! /usr/bin/env nix-shell
#! nix-shell -i bash -p bash cmake gcc python3 ninja libx11 libxrandr libxinerama libxcursor libxi libGL

set -e

cmake -S . -B build -G Ninja
cmake --build build --parallel
