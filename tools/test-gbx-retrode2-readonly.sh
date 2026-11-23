#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/migration
${CXX:-c++} -std=c++11 -Wall -Wextra -Werror -pedantic -O2 -Iinclude \
    src/gbx/retrode2_readonly.cpp tests/retrode2_gbx_readonly_test.cpp \
    -o build/migration/retrode2_gbx_readonly_test
./build/migration/retrode2_gbx_readonly_test
