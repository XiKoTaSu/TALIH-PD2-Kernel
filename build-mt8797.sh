#!/bin/bash
CLANG_PATH="$PWD/prebuilts/clang/host/linux-x86/clang-r383902"
CLANG_BIN="$CLANG_PATH/bin"
make O=out ARCH=arm64 CC=$CLANG_BIN/clang LD=$CLANG_BIN/ld.lld AR=$CLANG_BIN/llvm-ar NM=$CLANG_BIN/llvm-nm OBJCOPY=$CLANG_BIN/llvm-objcopy OBJDUMP=$CLANG_BIN/llvm-objdump READELF=$CLANG_BIN/llvm-readelf OBJSIZE=$CLANG_BIN/llvm-size STRIP=$CLANG_BIN/llvm-strip CROSS_COMPILE=aarch64-linux-gnu- LLVM=1 LLVM_IAS=1 ls12_mt8797_wifi_64_defconfig
make O=out ARCH=arm64 CC=$CLANG_BIN/clang LD=$CLANG_BIN/ld.lld AR=$CLANG_BIN/llvm-ar NM=$CLANG_BIN/llvm-nm OBJCOPY=$CLANG_BIN/llvm-objcopy OBJDUMP=$CLANG_BIN/llvm-objdump READELF=$CLANG_BIN/llvm-readelf OBJSIZE=$CLANG_BIN/llvm-size STRIP=$CLANG_BIN/llvm-strip CROSS_COMPILE=aarch64-linux-gnu- LLVM=1 LLVM_IAS=1 -j$(nproc)
