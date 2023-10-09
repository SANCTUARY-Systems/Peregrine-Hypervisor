#!/bin/bash
#
# Copyright 2018 The Hafnium Authors.
#
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/BSD-3-Clause.

# Note: this assumes that the images have all been built and the current working
# directory is the root of the repo.

# TIMEOUT, PROJECT, OUT, LOG_DIR_BASE set in:
KOKORO_DIR="$(dirname "$0")"
source $KOKORO_DIR/test_common.sh

SKIP_LONG_RUNNING_TESTS=false

while test $# -gt 0
do
  case "$1" in
    --skip-long-running-tests) SKIP_LONG_RUNNING_TESTS=true
      ;;
    *) echo "Unexpected argument $1"
      exit 1
      ;;
  esac
  shift
done

# Run the tests with a timeout so they can't loop forever.
HFTEST=(${TIMEOUT[@]} 300s ./test/hftest/hftest.py)
HYPERVISOR_PATH="$OUT/"

if [ $SKIP_LONG_RUNNING_TESTS == true ]
then
  HFTEST+=(--skip-long-running-tests)
fi

# Run the host unit tests.
mkdir -p "${LOG_DIR_BASE}/unit_tests"
LLVM_PROFILE_FILE="$OUT/default.profraw" ${TIMEOUT[@]} 30s "$OUT/host_fake_clang/unit_tests" \
  --gtest_output="json:${LOG_DIR_BASE}/unit_tests/sponge_log.json" \
  | tee "${LOG_DIR_BASE}/unit_tests/sponge_log.log"
