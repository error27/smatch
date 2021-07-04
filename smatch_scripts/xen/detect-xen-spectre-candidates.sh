#!/bin/bash

# Copyright (C) 2018 Amazon.com, Inc. or its affiliates.
# Author: Norbert Manthey <nmanthey@amazon.de>
#
# This script show cases how smatch can be run for Xen.
# You want to run this on a big machine, as Xen will be recompiled many times
# This script writes a file last_smatch_spectre_warns.txt, which will contain
# the spectre v1 candidates of smatch.
#
# Keep this script in its directory, but call it from the Xen root directory!

# Number of smatch iterations (guest taint improves per iteration)
MAX_ITERATIONS=8

# In case something breaks, we want to stop
set -e -u

# Where is this script located
SCRIPT=$(readlink -e "$0")
SCRIPT_DIR=$(dirname "$SCRIPT")

# Make sure we're in the Xen directory and Xen builds
echo "Check whether Xen builds ..."
make xen -j $(nproc)

if [ ! -d one-line-scan ]; then
    echo "Make one-line-scan tool available ..."
    git clone https://github.com/awslabs/one-line-scan.git
fi

# Tell environment about tools, and check whether they work
export PATH=$PATH:$(pwd)/smatch:$(pwd)/one-line-scan
echo "Test availability of smatch and one-line-scan ..."
for tool in smatch one-line-scan; do
    if ! command -v "$tool" &>/dev/null; then
        echo "Cannot find tool $tool, abort"
        exit 1
    fi
done
echo "Found all required tools, continue."

# Initialize variables for analysis
START=$SECONDS # start timestampe to print timing
OLD=0          # number of defects found in last iteration
NEW=0          # number of defects found in current iteration
I=0            # current iteration
BUILD_STATUS=0 # status of the analysis job

# Repeat analysis at most $MAX_ITERATIONS times
echo "Start Xen analysis with smatch, use $MAX_ITERATIONS iterations"
while [ "$I" -lt $MAX_ITERATIONS ]; do
    OLD=$NEW
    I=$((I + 1))
    # Write a log per iteration
    FULL_SPECTRE=1 ./smatch/smatch_scripts/build_xen_data.sh &>smatch-build-$I.log
    BUILD_STATUS=$?
    echo "build iteration $I with status $BUILD_STATUS"
    [ "$BUILD_STATUS" -eq 0 ] || exit $BUILD_STATUS

    # Keep results of last iteration around, in case the script is stopped early
    grep spectre smatch_warns.txt | sort -u >last_smatch_spectre_warns.txt

    # We are only interested in spectre issues for now
    NEW=$(cat last_smatch_spectre_warns.txt | wc -l)
    NOW=$SECONDS
    echo "new amount of defects: $NEW (last: $OLD) at iter $I after $((NOW - START))"

    # Check whether we found more defects
    [ "$NEW" -ne "$OLD" ] || break

done |& tee full-smatch.log

exit $BUILD_STATUS
