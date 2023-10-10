#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2018 The Hafnium Authors.
# SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.
# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only

"""Generate an initial RAM disk for the hypervisor.

Packages the VMs, initrds for the VMs and the list of secondary VMs (vms.txt)
into an initial RAM disk image.
"""

import argparse
import os
import shutil
import subprocess
import sys

def Main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-f", "--file",
        action="append", nargs=2,
        metavar=("NAME", "PATH"),
        help="File at host location PATH to be added to the RAM disk as NAME")
    parser.add_argument("-s", "--staging", required=True)
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args()

    # Create staging folder if needed.
    if not os.path.isdir(args.staging):
        os.makedirs(args.staging)

    # Copy files into the staging folder.
    staged_files = []
    for name, path in args.file:
        shutil.copyfile(path, os.path.join(args.staging, name))
        assert name not in staged_files
        staged_files.append(name)

    # Package files into an initial RAM disk.
    with open(args.output, "w") as initrd:
        # Move into the staging directory so the file names taken by cpio don't
        # include the path.
        os.chdir(args.staging)
        cpio = subprocess.Popen(
            ["cpio", "--create"],
            stdin=subprocess.PIPE,
            stdout=initrd,
            stderr=subprocess.PIPE)
        cpio.communicate(input="\n".join(staged_files).encode("utf-8"))
    return 0

if __name__ == "__main__":
    sys.exit(Main())
