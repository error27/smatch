#!/bin/bash

file=$1

if [[ "$file" = "" ]] ; then
    echo "Usage:  $0 <file with smatch messages>"
    exit 1
fi

grep -w free_arg $file | cut -d ' ' -f 3-

