# llvm2c

Translation of LLVM bitcode to C

## Dependencies

The only requirements are CMake in version at least 3.1 and LLVM 6 or 7.
The project should be compilable also on older LLVM's, but it is not tested
on these versions (patches are welcome :smile:)

## Building

```sh
git clone https://github.com/staticafi/llvm2c
cd llvm2c
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=installation/path/
make
```

## Testing

Just run `make check`.

### Testing with CSmith

To test `llvm2c` with [csmith](https://github.com/csmith-project/csmith/):

1. compile `csmith` and `llvm2c`
2. copy `csmith/src/csmith` binary to `test/` folder
3. copy `csmith/runtime` to `test/` folder and rename it to `csmith-runtime`
4. `cd test/`
5. `mkdir csmith-tests/`
6. `./csmith_testing.py`

The script generates 1000 testing programs. Each program is translated to LLVM
via `clang` and then back to C via `llvm2c`. The only thing that is currently
tested is, that `llvm2c` binary does not crash.

## Unsupported features

- vector instructions
- atomic operations
- some special intrinsics
- the code generation is currently fitted to x86\_64 bitcode

## Authors

* **Petr Vitovský** - [petrv7](https://github.com/petrv7)
* **Tomáš Jašek** - [tomsik68](https://github.com/tomsik68)
* **Marek Chalupa** - [mchalupa](https://github.com/mchalupa)
