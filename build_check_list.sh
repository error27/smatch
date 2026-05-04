#!/bin/bash

TMP_FILE=$(mktemp)
OUTFILE=smatch_checks.h

cat << EOF > ${TMP_FILE}

#ifndef CK
#define CK(_x) void _x(int id);
#define __undo_CK_def
#endif
EOF

ls check_*.c | sed 's/^\(.*\).c$/CK(\1)/' | sort >> ${TMP_FILE}

cat << EOF >> ${TMP_FILE}

#ifdef __undo_CK_def
#undef CK
#undef __undo_CK_def
#endif
EOF

cmp -s ${TMP_FILE} ${OUTFILE} || mv ${TMP_FILE} ${OUTFILE}

rm -f ${TMP_FILE}
