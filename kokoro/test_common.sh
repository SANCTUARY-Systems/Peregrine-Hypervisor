#!/bin/bash
# SPDX-FileCopyrightText: 2020 The Hafnium Authors.
# SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.
# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only

# Fail on any error.
set -e
# Fail on any part of a pipeline failing.
set -o pipefail
# Treat unset variables as an error.
set -u
# Display commands being run.
set -x

TIMEOUT=(timeout --foreground)
PROJECT="${PROJECT:-host_test}"
OUT="out/${PROJECT}"
LOG_DIR_BASE="${OUT}/kokoro_log"

# Set path to prebuilts used in the build.
UNAME_S=$(uname -s | tr '[:upper:]' '[:lower:]')
UNAME_M=$(uname -m)

if [ $UNAME_M == "x86_64" ]
then
        UNAME_M=x64
fi

export PREBUILTS="$PWD/prebuilts/${UNAME_S}-${UNAME_M}"
export LD_LIBRARY_PATH="$(clang --print-resource-dir)/../.."

