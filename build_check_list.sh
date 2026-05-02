#!/bin/bash

OUTFILE=smatch_checks.h

cat << EOF > ${OUTFILE}

#ifndef CK
#define CK(_x) void _x(int id);
#define __undo_CK_def
#endif
EOF

#for i in check_*.c ; do
#    NO_SUF=$(echo $i | sed -e 's/.c$//')
#    if grep -qw $NO_SUF smatch_modules*.h ; then
#        continue
#    fi
#    echo $NO_SUF | sed 's/^\(.*\)$/CK(\1)/' >> ${OUTFILE}
#done

ls check_*.c | sed 's/^\(.*\).c$/CK(\1)/' >> ${OUTFILE}

cat << EOF >> ${OUTFILE}

#ifdef __undo_CK_def
#undef CK
#undef __undo_CK_def
#endif
EOF
