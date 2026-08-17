#!/bin/sh

ARCH="$1"
export ARCH
shift

../smatch "$@"
