#!/bin/bash

file=$1

if [[ "$file" = "" ]] ; then
    echo "Usage:  $0 <file with smatch messages>"
    exit 1
fi

grep 'if();' $file | cut -d ' ' -f1,2 | while read code_file line ; do
    echo $code_file $line
    tail -n $line $code_file | head -n 1
done

