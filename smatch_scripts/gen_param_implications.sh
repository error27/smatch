#!/bin/bash

file=$1

if [[ "$file" = "" ]] ; then
    echo "Usage:  $0 <file with smatch messages>"
    exit 1
fi

outfile="kernel.parameter_implications"

bin_dir=$(dirname $0)
add=$(echo ${bin_dir}/../smatch_data/${outfile}.add)

cat $add > $outfile
${bin_dir}/parameter_implications.pl $file >> $outfile

echo "Done.  List saved as '$outfile'"
