#! /usr/bin/env nix-shell
#! nix-shell -i bash -p 

set -e
./build.sh
./build/tester
