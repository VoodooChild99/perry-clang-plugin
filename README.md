# Perry Clang Plugin
This is a clang plugin intended to be used with [Perry](https://github.com/VoodooChild99/perry). The plugin identifies:

* Names of potential APIs
* Ranges of loops in source code
* Names of functions that indicate success returns using enums

Results are written to files specified by users in YAML format. To specify output files, provide a path to `-out-file-api`, `-out-file-loops`, and `-out-file-succ-ret`, respectively. The generated files can be used by Perry.

You may load the plugin manually, or use the provided (clang) compiler wrapper to automatically do that for you.

## Tested Environment
* Ubuntu 20.04
* LLVM 13

## Build
```bash
mkdir build
cd build
cmake -DPERRY_Clang_INSTALL_DIR=<Path to LLVM/Clang Installation (e.g., /usr/local)> ..
make
```

## Usage
**Option 1: use the provided compiler wrapper**: replace the original compiler with `/path/to/perry-clang/plugin/build/compiler/{perry-clang/perry-clang++}`

**Option 2: manually load the plugin**: add the following flags to clang/clang++:

```-Xclang -load -Xclang </path/to/the/plugin> -Xclang -add-plugin -Xclang perry -Xclang -plugin-arg-perry -Xclang -out-file-succ-ret -Xclang -plugin-arg-perry -Xclang <path> -Xclang -plugin-arg-perry -Xclang -out-file-api -Xclang -plugin-arg-perry -Xclang <path> -Xclang -plugin-arg-perry -Xclang -out-file-loops -Xclang -plugin-arg-perry -Xclang <path>```