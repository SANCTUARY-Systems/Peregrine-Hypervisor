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

USE_FVP=false
USE_TFA=false
SKIP_LONG_RUNNING_TESTS=false
RUN_ALL_QEMU_CPUS=false

while test $# -gt 0
do
  case "$1" in
    --fvp) USE_FVP=true
      ;;
    --tfa) USE_TFA=true
      ;;
    --skip-long-running-tests) SKIP_LONG_RUNNING_TESTS=true
      ;;
    --run-all-qemu-cpus) RUN_ALL_QEMU_CPUS=true
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
if [ $USE_FVP == true ]
then
  HYPERVISOR_PATH+="aem_v8a_fvp_clang"
  HFTEST+=(--driver=fvp)
  HFTEST+=(--out_initrd "$OUT/aem_v8a_fvp_vm_clang")
  HFTEST+=(--out_partitions "$OUT/aem_v8a_fvp_vm_clang")
else
  HYPERVISOR_PATH+="qemu_aarch64_clang"
  HFTEST+=(--out_initrd "$OUT/qemu_aarch64_vm_clang")
fi
if [ $USE_TFA == true ]
then
  HFTEST+=(--tfa)
fi
if [ $SKIP_LONG_RUNNING_TESTS == true ]
then
  HFTEST+=(--skip-long-running-tests)
fi

# Run the host unit tests.
mkdir -p "${LOG_DIR_BASE}/unit_tests"
${TIMEOUT[@]} 30s "$OUT/host_fake_clang/unit_tests" \
  --gtest_output="xml:${LOG_DIR_BASE}/unit_tests/sponge_log.xml" \
  | tee "${LOG_DIR_BASE}/unit_tests/sponge_log.log"

CPUS=("")

if [ $RUN_ALL_QEMU_CPUS == true ]
then
  CPUS=("cortex-a53" "max")
fi

for CPU in "${CPUS[@]}"
do
  HFTEST_CPU=("${HFTEST[@]}")
  if [ -n "$CPU" ]
  then
    # Per-CPU log directory to avoid filename conflicts.
    HFTEST_CPU+=(--cpu "$CPU" --log "$LOG_DIR_BASE/$CPU")
  else
    HFTEST_CPU+=(--log "$LOG_DIR_BASE")
  fi

  "${HFTEST_CPU[@]}" --hypervisor "$HYPERVISOR_PATH/arch_test.bin"
  if [ $USE_TFA == true -o $USE_FVP == true ]
  then
    "${HFTEST_CPU[@]}" --hypervisor "$HYPERVISOR_PATH/aarch64_test.bin"
  fi

  "${HFTEST_CPU[@]}" --hypervisor "$HYPERVISOR_PATH/hafnium.bin" \
                     --initrd test/vmapi/arch/aarch64/aarch64_test

  "${HFTEST_CPU[@]}" --hypervisor "$HYPERVISOR_PATH/hafnium.bin" \
                     --initrd test/vmapi/arch/aarch64/gicv3/gicv3_test

  "${HFTEST_CPU[@]}" --hypervisor "$HYPERVISOR_PATH/hafnium.bin" \
                     --initrd test/vmapi/primary_only/primary_only_test

  "${HFTEST_CPU[@]}" --hypervisor "$HYPERVISOR_PATH/hafnium.bin" \
                     --initrd test/vmapi/primary_with_secondaries/primary_with_secondaries_test

  "${HFTEST_CPU[@]}" --hypervisor "$HYPERVISOR_PATH/hafnium.bin" \
                     --initrd test/vmapi/primary_with_secondaries/primary_with_secondaries_no_fdt

  "${HFTEST_CPU[@]}" --hypervisor "$HYPERVISOR_PATH/hafnium.bin" \
                     --initrd test/linux/linux_test \
                     --force-long-running --vm_args "rdinit=/test_binary --"

  # TODO: Get Trusty tests working on FVP too.
  if [ $USE_TFA == true ]
  then
    "${HFTEST_CPU[@]}" --hypervisor "$HYPERVISOR_PATH/hafnium.bin" \
                       --initrd test/vmapi/arch/aarch64/trusty/trusty_test
  fi

  if [ $USE_TFA == true ] && [ $USE_FVP == true ]
  then
     "${HFTEST_CPU[@]}" --hypervisor "$HYPERVISOR_PATH/hafnium.bin" \
                        --partitions_json test/vmapi/primary_only_ffa/primary_only_ffa_test.json
  fi
done
