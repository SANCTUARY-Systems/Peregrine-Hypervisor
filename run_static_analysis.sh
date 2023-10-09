#!/bin/bash
rm -rf ./reports
make clean
~/.local/bin/CodeChecker log --build "make" --output ./compile_commands.json
sed -i -e 's/\-nostdinc//g' compile_commands.json
PATH="$PWD/prebuilts/linux-x64/clang/bin/:$PATH" ~/.local/bin/CodeChecker analyze ./compile_commands.json --enable sensitive --ctu --ignore skip_file --output ./reports
