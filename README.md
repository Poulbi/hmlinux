# Poulbi's Handmade Hero Linux port

## Overview
To make sure I internalize the Handmade Hero series, I am porting the game over to Linux.

## Usage
Create a `handmade.cpp` and implement `handmade_platform.h`.  It will then get compiled into a
shared object by the build script.

## Build
Run the build script.  This will create a `build` directory in the parent directory.
```sh
./build.sh
```

## Running
```sh
../build/linux_handmade
```

# Resources
- [Episode Guide](https://guide.handmadehero.org/)
