# Handmade Linux platform layer

Should work until day 52.

## Installation
You can run the installation script.
This will create a `hm_linux` directory with all library files in the current directory.
```sh
./install.sh
```

There is an example `./build.sh` that you can use. 
It assumes the following project structure:

```
.
└── code
    ├── libs
    │   └── hm_linux
    ├── build.sh
    └── handmade.cpp
```

You can then build with `./code/build.sh`.
