#!/bin/sh
set -eu
cd "$(dirname "$0")"
make -j"$(nproc)" "$@"