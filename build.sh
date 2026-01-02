#!/bin/sh

set -e

ThisDir="$(dirname "$(readlink -f "$0")")"
cd "$ThisDir"

mkdir -p ../build > /dev/null 2>&1
cd ../build

# Default arguments
clang=1
debug=1

for arg in "$@"; do eval "$arg=1"; done
[ "$release" ] && debug=
[ "$gcc" ]     && clang=
[ "$windows" ] && clang=

[ "$gcc" ]     && Compiler="g++"
[ "$clang" ]   && Compiler="clang"
[ "$windows" ] && Compiler="x86_64-w64-mingw32-c++" && gcc=1

[ "$debug" ]   && printf '[debug mode]\n'
[ "$release" ] && printf '[release mode]\n'
printf '[%s compile]\n' "$Compiler"

CommonCompilerFlags="
-DOS_LINUX=1
-DHANDMADE_PROFILING=0
-DHANDMADE_SMALL_RESOLUTION=1
-DHANDMADE_FORCE_UPDATEHZ=30
-fsanitize-trap
-nostdinc++
"
# You can also use -fsanitize=address & -fsanitize=undefined & -fsanitize=thread
# I wish I could add: -nostartfiles -nodefaultlibs -nostdlib

CommonWarningFlags="
-Wall
-Wextra
-Wconversion -Wdouble-promotion
-Wno-sign-conversion
-Wno-sign-compare
-Wno-double-promotion
-Wno-unused-but-set-variable
-Wno-unused-variable
-Wno-write-strings
-Wno-pointer-arith
-Wno-unused-parameter
-Wno-unused-function
"

ClangCompilerFlags="
-fdiagnostics-absolute-paths
-ftime-trace
"
ClangWarningFlags="
-Wno-null-dereference
-Wno-missing-braces
-Wno-vla-cxx-extension
-Wno-writable-strings
-Wno-missing-designated-field-initializers
-Wno-address-of-temporary
-Wno-int-to-void-pointer-cast
"

GCCWarningFlags="
-Wno-cast-function-type
-Wno-missing-field-initializers
-Wno-int-to-pointer-cast
"

LinuxLinkerFlags="
-lpthread
-lm
-lasound
-lX11
-lXfixes"

WindowsLinkerFlags="
-luser32
-lgdi32
-lwinmm
"

if [ "$debug" ]
then
 CommonCompilerFlags="$CommonCompilerFlags
-O0
-g -ggdb -g3
-DHANDMADE_SLOW=1
-DHANDMADE_INTERNAL=1
"
elif [ "$release" ]
then
 CommonCompilerFlags="$CommonCompilerFlags
-O3
"
fi

if [ "$gcc" ]
then
 CommonWarningFlags="$CommonWarningFlags $GCCWarningFlags"
fi

if [ "$clang" ]
then
 CommonCompilerFlags="$CommonCompilerFlags $ClangCompilerFlags"
 CommonWarningFlags="$CommonWarningFlags $ClangWarningFlags"
fi

if [ "$windows" ]
then
 printf 'handmade.dll\n'
 $Compiler \
  $CommonCompilerFlags \
  $CommonWarningFlags \
  -shared -fPIC \
  -o $ThisDir/../build/handmade.dll \
  $ThisDir/handmade.cpp 
 
 printf 'win32_handmade.exe\n'
 $Compiler \
  $CommonCompilerFlags \
  $CommonWarningFlags \
  -o $ThisDir/../build/win32_handmade \
  $ThisDir/libs/hm_win32/win32_handmade.cpp \
  $WindowsLinkerFlags
fi

if [ -z "$windows" ]
then


 printf 'handmade.so\n'
 $Compiler \
  $CommonCompilerFlags \
  $CommonWarningFlags \
  -fPIC \
  -shared \
  -o handmade.so \
  ../code/handmade.cpp
 
 printf 'linux_handmade\n'
 $Compiler \
  $CommonCompilerFlags \
  $CommonWarningFlags \
  $LinuxLinkerFlags \
  -o linux_handmade \
  ../code/libs/hm_linux/linux_handmade.cpp
fi
