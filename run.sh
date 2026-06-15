#!/bin/bash
cd "$(dirname "$0")"
make clean && make
exec ./build-kbuild/agent "$@" 2>&1 | tee /tmp/agent.log
