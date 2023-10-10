#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2018 The Hafnium Authors.
# SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.
# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only

"""Convert a file to binary format.

Calls objcopy to convert a file into raw binary format.
"""

import argparse
import os
import subprocess
import sys

OBJCOPY = "llvm-objcopy"

def Main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    subprocess.check_call([
        OBJCOPY, "-O", "binary", args.input, args.output
    ])
    return 0


if __name__ == "__main__":
    sys.exit(Main())
