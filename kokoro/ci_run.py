#!/usr/bin/env python3

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
