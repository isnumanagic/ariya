# ariya

To build:
```bash
clang++ -g main.cpp \
    -fdiagnostics-color=always \
    -std=c++14 \
    -fno-exceptions \
    -I/usr/lib/llvm-13/include \
    -D_GNU_SOURCE \
    -D__STDC_CONSTANT_MACROS \
    -D__STDC_FORMAT_MACROS \
    -D__STDC_LIMIT_MACROS \
    -L/usr/lib/llvm-13/lib \
    -lLLVM-13 \
    -o main
```

To run parser:
```bash
./main
```

To run resulting .ll:
```bash
lli main.ll
```
