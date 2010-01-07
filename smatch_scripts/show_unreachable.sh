#!/bin/bash

context=6
if [ "$1" = "-C" ] ; then
    shift
    context=$1
    shift
fi

file=$1
if [[ "$file" = "" ]] ; then
    echo "Usage:  $0 [-C] <file with smatch messages>"
    exit 1
fi

grep 'ignoring unreachable' $file | cut -d ' ' -f1,2 | while read code_file line ; do
    echo "========================================================="
    echo $code_file $line
    tail -n +$(($line - ($context - 1))) $code_file | head -n $(($context - 1))
    echo "---------------------------------------------------------"
    tail -n $line $code_file | head -n $context
done

