#!/bin/bash -e

usage()
{
    echo "usage:  $0 <patch file>"
    exit 1
}

if [ "$1" = "" ] ; then
    usage
fi

SCRIPT_DIR=$(dirname $0)
if [ -e $SCRIPT_DIR/kchecker ] ; then
    KCHECKER=$SCRIPT_DIR/kchecker
elif which kchecker | grep kchecker > /dev/null ; then
    KCHECKER=kchecker
else
    echo "$SCRIPT_DIR"
    echo "kchecker script not found."
    exit 1
fi

PATCH=$1

files=$(grep ^+++ $PATCH | cut -f 1 | cut -b 5-)
if [ "$files" = "" ] ; then
    usage
fi

if ! cat $PATCH | patch -p1 --dry-run > /dev/null ; then
    echo "Couldn't apply patch"
    exit 1
fi

before=$(mktemp /tmp/before.XXXXXXXXXX)
after=$(mktemp /tmp/after.XXXXXXXXXX)

for file in $files ; do
    file=${file#*/}
    if ! echo $file | grep c$ ; then
	continue
    fi

    $KCHECKER --test-parsing --outfile=$before $file
    cat $PATCH | patch -p1
    $KCHECKER --test-parsing --outfile=$after $file
    cat $PATCH | patch -p1 -R

    if [ ! -s $before ] ; then
	echo "Error:  No result"
	exit 1
    fi

    if diff $before $after > /dev/null ; then
	echo
	echo Only white space changed
	echo
    else
	echo '!!#$%@$%@^@#$^@#%@$%@$%@#%$@#%!!'
	echo '!!                            !!'
	echo '!!  This patch changes stuff  !!'
	echo '!!                            !!'
	echo '!!#$%@$%@^@#$^@#%@$%@$%@#%$@#%!!'
    fi
    rm -f $before $after
done

