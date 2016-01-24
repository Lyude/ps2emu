#!/bin/sh

# This could fail, so swallow the error if it does
make distclean ||:
docker build -t lyude/ps2emu .
