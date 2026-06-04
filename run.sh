#!/bin/bash

./build-deb.sh

sudo apt install --reinstall ./dist/daima-agent_0.1.0_amd64.deb

daima 2>&1 | tee /tmp/daima.log