#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2019 The Hafnium Authors.
# SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.
# SPDX-License-Identifier: GPL-2.0-only

"""Parse Repo manifest and symlink files specified in <linkfile> tags.

This is a workaround for Kokoro which does not support <linkfile>.
"""

import argparse
import os
import sys
import xml.etree.ElementTree as ET

def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("root_dir", help="root directory")
	args = parser.parse_args()

	manifest = os.path.join(args.root_dir, ".repo", "manifest.xml")
	tree = ET.parse(manifest)
	root = tree.getroot()
	assert(root.tag == "manifest");

	for proj in root:
		if proj.tag != "project":
			continue

		proj_name = proj.attrib["name"]
		proj_path = proj.attrib["path"]

		for linkfile in proj:
			if linkfile.tag != "linkfile":
				continue

			linkfile_src = linkfile.attrib["src"]
			linkfile_dest = linkfile.attrib["dest"]
			src_path = os.path.join(
				args.root_dir, proj_path, linkfile_src)
			dest_path = os.path.join(args.root_dir, linkfile_dest)

			os.symlink(src_path, dest_path)

if __name__ == "__main__":
    sys.exit(main())
