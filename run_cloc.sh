#!/bin/bash
cloc --json --out=cloc.json --not-match-f=".*_(test|fake).(c|cc|h|hh)" --not-match-d="(fake|hftest)" src inc