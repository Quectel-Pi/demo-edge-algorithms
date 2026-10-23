#!/bin/sh
set -eu
cd "$(dirname "$0")"
make -j"$(getconf _NPROCESSORS_ONLN)" "$@"