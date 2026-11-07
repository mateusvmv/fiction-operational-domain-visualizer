#! /usr/bin/env nix-shell
#! nix-shell -i bash -p bash cmake gcc python3 ninja libx11 libxrandr libxinerama libxcursor libxi libGL gf

set -e

mkdir -p target
if [ "build.c" -nt "target/build" ]; then
    cc -g build.c -o target/build
fi

exec ./target/build "$@"
