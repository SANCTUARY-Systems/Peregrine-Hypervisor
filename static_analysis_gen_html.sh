#!/bin/bash
rm -rf reports_html
CodeChecker parse --export html --output ./reports_html ./reports
