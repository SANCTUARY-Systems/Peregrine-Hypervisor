#!/usr/bin/env python3

# Copyright (c) 2023 SANCTUARY Systems GmbH
#
# This file is free software: you may copy, redistribute and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 2 of the License, or (at your
# option) any later version.
#
# This file is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
# For a commercial license, please contact SANCTUARY Systems GmbH
# directly at info@sanctuary.dev


from pathlib import Path
import json
import subprocess

json_file = Path("out/host_test/kokoro_log/unit_tests/sponge_log.json")

subprocess.call("make clean", shell=True)
subprocess.call("make clobber", shell=True)

rc = subprocess.call("./kokoro/build.sh", shell=True)



if json_file.is_file:
    with open(json_file, "r") as f:
        test_log = json.load(f)
        tests_total = test_log["tests"]
        tests_errors = test_log["errors"] + test_log["failures"]
        print(f"{tests_total - tests_errors}/{tests_total}")
