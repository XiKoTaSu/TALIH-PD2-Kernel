CLANG_PATH="$PWD/prebuilts/clang/host/linux-x86/clang-r383902"
CLANG_BIN="$CLANG_PATH/bin"
if [ ! -f "$CLANG_BIN/clang" ]; then
curl -L -o "$CLANG_BIN/clang" "https://github.com/Kevin233B/TALIH-PD2-Kernel/releases/download/other/clang"
chmod +x "$CLANG_BIN/clang"
fi
