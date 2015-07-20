#!/bin/sh

set -e

if [ "$1" != "--no-submodule-init" ]; then
    git submodule init
    git submodule update
fi

[ -d "build-aux" ] || mkdir build-aux
autoreconf --install -v --force

[ "$NOCONFIGURE" == 1 ] || ./configure $@
