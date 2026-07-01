#!/bin/bash
set -e
CLANG_PATH="prebuilts/clang/host/linux-x86/clang-r383902"
CLANG_BIN="$CLANG_PATH/bin"
if [ ! -f "$CLANG_BIN/clang" ]; then
mkdir -p "$CLANG_PATH"
curl -L -o "$CLANG_PATH/clang" "https://github.com/Kevin233B/TALIH-PD2-Kernel/releases/download/other/clang"
chmod +x "$CLANG_PATH/clang"
fi
curl -LSs "https://raw.githubusercontent.com/SukiSU-Ultra/SukiSU-Ultra/main/kernel/setup.sh" | bash -s main
export PATH="$PWD/$CLANG_BIN:$PATH"
export LD_LIBRARY_PATH="$PWD/$CLANG_PATH/lib64:$LD_LIBRARY_PATH"
make O=out ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump READELF=llvm-readelf OBJSIZE=llvm-size STRIP=llvm-strip CROSS_COMPILE=aarch64-linux-gnu- LLVM=1 LLVM_IAS=1 ls12_mt8797_wifi_64_defconfig
make O=out ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump READELF=llvm-readelf OBJSIZE=llvm-size STRIP=llvm-strip CROSS_COMPILE=aarch64-linux-gnu- LLVM=1 LLVM_IAS=1 -j$(nproc)