#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2019 The Hafnium Authors.
# SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.
# SPDX-License-Identifier: GPL-2.0-only

"""Generate an initial RAM disk for a Linux VM."""

import argparse
import os
import shutil
import subprocess
import sys

def Main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--staging", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    # Package files into an initial RAM disk.
    with open(args.output, "w") as initrd:
        # Move into the staging directory so the file names taken by cpio don't
        # include the path.
        os.chdir(args.staging)
        staged_files = [os.path.join(root, filename)
          for (root, dirs, files) in os.walk(".") for filename in files + dirs]
        cpio = subprocess.Popen(
            ["cpio", "--create", "--format=newc"],
            stdin=subprocess.PIPE,
            stdout=initrd,
            stderr=subprocess.PIPE)
        cpio.communicate(input="\n".join(staged_files).encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(Main())
