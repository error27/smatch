#!/bin/bash -e

TMP_DIR=/tmp

help()
{
    echo "Usage: $0 <filename>"
    echo "You must be at the base of the kernel tree to run this."
    exit 1
}

signoff()
{
    name=$(mutt -Q realname | cut -d '"' -f 2)
    [ "$name" = "" ] && name="Your Name"
    email=$(mutt -Q from | cut -d '"' -f 2)
    [ "$email" = "" ] && email="address@example.com"
    echo "Signed-off-by: $name <${email}>"
}

continue_yn()
{
    echo -n "Do you want to fix these check patch errors now? "
    read ans
    if ! echo $ans | grep -iq ^n ; then
	exit 1;
    fi
}

qc()
{
    local msg=$1
    local ans

    echo -n "$msg:  "
    read ans
    if ! echo $ans | grep -qi ^y ; then
	exit 1
    fi
}

if [ ! -f $1 ] ; then
    help
fi

MY_SIGNOFF=$(signoff)

fullname=$1
filename=$(basename $fullname)

DIFF_FILE=$TMP_DIR/${filename}.diff
MAIL_FILE=$TMP_DIR/${filename}.msg

echo "QC checklist"
qc "Have you handled all the errors properly?"
if git diff $fullname | grep ^+ | grep -qi alloc ; then
    qc "Have you freed all your mallocs?"
fi

kchecker --spammy $fullname
kchecker --sparse $fullname
echo "Press ENTER to continue"
read unused

to_addr=$(./scripts/get_maintainer.pl -f $fullname | head -n 1)
cc_addr=$(./scripts/get_maintainer.pl -f $fullname | tail -n +2 | \
    perl -ne 's/\n$/, /; print')
cc_addr="$cc_addr, kernel-janitors@vger.kernel.org"

echo -n "To:  "  > $MAIL_FILE
echo "$to_addr" >> $MAIL_FILE
echo -n "CC:  " >> $MAIL_FILE
echo "$cc_addr" >> $MAIL_FILE

echo "" > $DIFF_FILE
echo "$MY_SIGNOFF" >> $DIFF_FILE
echo "" >> $DIFF_FILE

git diff $fullname | tee -a $DIFF_FILE

./scripts/checkpatch.pl $DIFF_FILE || continue_yn

echo "Press ENTER to continue"
read unused

cat $DIFF_FILE >> $MAIL_FILE

mutt -H $MAIL_FILE
