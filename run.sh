#!/bin/bash
cd "$(dirname "$0")"
make clean && make
exec ./build-kbuild/daima "$@" 2>&1 | tee /tmp/daima.log
