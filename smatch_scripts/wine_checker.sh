#!/bin/bash

function useage {
    echo "Usage:  $0 [--sparse][--valgrind][--debug] path/to/file.c"
    exit 1
}

CMD=smatch
POST=""
WINE_ARGS="-p=wine --full-path -D__i386__"

while true ; do
    if [[ "$1" == "--sparse" ]] ; then
	CMD="sparse"
	shift
    elif [[ "$1" == "--valgrind" ]] ; then
	PRE="valgrind"
	shift
    elif [[ "$1" == "" ]] ; then
	break
    else
	if [[ "$1" == "--help" ]] ; then
		$CMD --help
		exit 1
	fi
	if echo $1 | grep -q ^- ; then
		POST="$POST $1"
	else
		break
	fi
	shift
    fi
done

cname=$1
cname=$(echo ${cname/.o/.c})
if [[ "$cname" == "" ]] ; then
    useage
fi
if ! test -e $cname ; then
    useage
fi

oname=$(echo ${cname/.c/.o})
if ! echo $oname | grep .o$ > /dev/null ; then
    useage
fi
rm -f $oname

cur=$(pwd)
file_dir=$(dirname $oname)
o_short_name=$(basename $oname)
cd $file_dir
make CC="$PRE $CMD $POST $WINE_ARGS" $o_short_name
make $o_short_name
cd $cur
