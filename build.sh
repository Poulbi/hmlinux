#!/bin/sh

ThisDir="$(dirname "$(readlink -f "$0")")"
cd "$ThisDir"

mkdir ../build > /dev/null 2>&1

CompilerFlags="-ggdb -DHANDMADE_PROFILING -DHANDMADE_INTERNAL -DHANDMADE_SLOW"
WarningFlags="-Wall -Wextra -Wno-unused-but-set-variable -Wno-unused-variable -Wno-write-strings -Wno-unused-parameter -Wno-unused-function"

printf 'handmade.cpp\n'
g++ $CompilerFlags $WarningFlags -shared -o ../build/handmade.so handmade.cpp

printf 'linux_handmade.cpp\n'
g++ $CompilerFlags $WarningFlags -o ../build/linux_handmade linux_handmade.cpp -lasound -lm -lX11 -lXfixes
