#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1 in the browser without Emscripten: web/emu/felucca_web.c (FELUCCA_FMINI_ONLY) compiled with wasi-sdk
# (https://github.com/WebAssembly/wasi-sdk, a reactor module with no imports) plus the page.
#   WASI_SDK=/path/to/wasi-sdk web/emu/build_wasi.sh   ->  ${OUT:-docs/emu}/{index.html, worklet.js, felucca.wasm, fonts/}
# Run after one ./build.sh (build/gen: the tables, the fonts, the CC0 samples).
set -e
cd "$(dirname "$0")/../.."
: "${WASI_SDK:?set WASI_SDK to a wasi-sdk directory (wasi-sdk-25.0-x86_64-linux)}"
#   SMK=1 ...: FMini-1 on the SMK-37 (BOARD_SMK37, web/emu/smk_web_input.h; the panel web/emu/smk/) -> ${OUT:-docs/emu/smk}
[ -f build/gen/felucca_tables.h ] || { echo "emu: run ./build.sh once first (build/gen)"; exit 1; }
if [ "${SMK:-0}" = 1 ]; then
    OUT=${OUT:-docs/emu/smk}
    VERSION=$(sed -n 's/^#define FMINI_VERSION "\([^"]*\)".*/\1/p' firmware/src/core.h | head -n 1)
    BOARD="-DBOARD_SMK37=1 -Itests/smk_fake"     # (smk_midi_out.c's TRS UART: the test's recorder)
    PAGE=web/emu/smk
else
    OUT=${OUT:-docs/emu}
    VERSION=$(sed -n 's/^#define FMINI_VERSION "\([^"]*\)".*/\1/p' firmware/src/core.h | head -n 1)
    BOARD=
    PAGE=web/emu
fi
mkdir -p "$OUT"
"$WASI_SDK/bin/clang" --sysroot="$WASI_SDK/share/wasi-sysroot" --target=wasm32-wasi -mexec-model=reactor \
    ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w "-DFELUCCA_VERSION=\"$VERSION\"" $BOARD \
    -Ibuild/gen -Ifirmware/src -Iweb/emu/wasi_shim \
    -Wl,--export-dynamic -Wl,-z,stack-size=1048576 -Wl,--initial-memory=33554432 \
    web/emu/felucca_web.c -o "$OUT/felucca.wasm" -lm
sed -e "s/@VERSION@/FMini-1 $VERSION/g" -e "s/@PREVIEW@//g" "$PAGE/index.html" > "$OUT/index.html"
cp "$PAGE/worklet.js" "$OUT/"
if [ "${SMK:-0}" != 1 ]; then
    mkdir -p "$OUT/fonts"
    cp web/emu/fonts/DotGothic16-subset.woff web/emu/fonts/OFL.txt "$OUT/fonts/"
fi
echo "emu: $OUT ($(wc -c < "$OUT/felucca.wasm" | tr -d ' ') B wasm, $VERSION)"
