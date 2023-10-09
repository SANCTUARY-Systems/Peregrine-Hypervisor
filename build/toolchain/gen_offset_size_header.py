#!/usr/bin/env python3
#
# Copyright 2019 The Hafnium Authors.
#
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/BSD-3-Clause.

"""Generate a header file with definitions of constants parsed from a binary."""

import argparse
import os
import re
import subprocess
import sys
import tempfile

STRINGS = "llvm-strings"
AR = "llvm-ar"
DIS = "llvm-dis"


PROLOGUE = """
/**
** This file was auto-generated by {}.
** Changes will be overwritten.
**/

#pragma once

""".format(__file__)


def process_binfile(f):
	if not f.endswith('.a'):
		# print("Not an archive")
		yield ('S', subprocess.check_output([STRINGS, f]))
	else:  # ar archive
		# print("Archive file")
		f = os.path.abspath(f)
		with tempfile.TemporaryDirectory() as tmpdir:
			subprocess.check_call([AR, 'x', f], cwd=tmpdir)
			for ff in os.listdir(tmpdir):
				fff = os.path.join(tmpdir, ff)
				with open(fff, 'rb') as fd:
					magic = fd.read(2)
				if magic != b'BC':  # normal object file
					# print(f"Processing {fff} as normal object file")
					yield ('S', subprocess.check_output([STRINGS, fff]))
				else:  # LLVM bytecode file
					# print(f"Processing {fff} as LLVM bytecode")
					yield ('L', subprocess.check_output([DIS, fff, '-o', '-']))


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("bin_file",
		help="binary file to be parsed for definitions of constants")
	parser.add_argument("out_file", help="output file");
	args = parser.parse_args()

	# Regex for finding definitions: <HAFNIUM_DEFINE name value />
	regex = re.compile(r'<HAFNIUM_DEFINE\s([A-Za-z0-9_]+)\s([0-9]+) />')
	bcregex = re.compile(r'<HAFNIUM_DEFINE\s([A-Za-z0-9_]+) \$0 />.* "i"\(i64 (\d+)\)')

	with open(args.out_file, "w") as f:
		f.write(PROLOGUE)

		for ty, dump in process_binfile(args.bin_file):
			if ty == 'S':
				r = regex
			else:
				assert ty == 'L'
				r = bcregex

			# Extract strings from the input binary file.
			dump = dump.decode('utf-8').split(os.linesep)

			for line in dump:
				for match in r.findall(line):
					# print(match)
					f.write("#define {} ({})\n".format(
						match[0], match[1]))


if __name__ == "__main__":
    sys.exit(main())
