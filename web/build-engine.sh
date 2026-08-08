#!/bin/bash
# Builds the Emscripten engine and produces a browser-loadable
# alephone.js/.wasm pair (an ES module, MODULARIZE'd, with FS/callMain
# exported) into web/engine/. See WEB_PORT_PLAN.md, M4.
#
# Assumes build-wasm/ has already been configured via emconfigure (see
# WEB_PORT_PLAN.md for the recipe) -- this script only builds and relinks.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-wasm}"
OUT_DIR="${OUT_DIR:-$REPO_ROOT/web/engine}"

if [ -f "$REPO_ROOT/../emsdk/emsdk_env.sh" ]; then
  # shellcheck source=/dev/null
  source "$REPO_ROOT/../emsdk/emsdk_env.sh" >/dev/null
fi

if ! command -v emmake >/dev/null; then
  echo "emmake not on PATH -- source emsdk_env.sh first" >&2
  exit 1
fi

if [ ! -f "$BUILD_DIR/Source_Files/Makefile" ]; then
  echo "$BUILD_DIR is not configured yet; run emconfigure first (see WEB_PORT_PLAN.md)" >&2
  exit 1
fi

# AR/RANLIB=emar/emranlib, not the native macOS/Linux ar/ranlib: autotools
# doesn't switch these automatically just because CC/CXX are emcc/em++, and
# the native tools don't understand WASM object files -- they were silently
# producing corrupt .a archives (real symptom: "wasm-ld: LLVM ERROR:
# malformed uleb128, extends past end" at final link time, only once an
# archive actually got rewritten). See WEB_PORT_PLAN.md, M4.
emmake make -C "$BUILD_DIR" AR=emar RANLIB=emranlib \
  -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

# Automake's link rule (see Source_Files/Makefile's CXXLINK definition) puts
# "-o alephone" right after the compiler flags, followed by the full object/
# library list -- not at the end of the line, so there's no LDFLAGS override
# that redirects it to .html/.js output (whatever we pass in LDFLAGS would
# land *before* automake's own "-o alephone", which would then win). Instead,
# capture the real link command (correct object/library list, computed by
# automake) via a forced dry run, and re-run it ourselves with an extra -o
# appended at the very end -- since emcc (like clang) honors the *last* -o
# flag, that's enough to redirect the output without editing the original.
echo "Extracting link command..."
LINK_CMD=$(make -C "$BUILD_DIR/Source_Files" -n -B V=1 alephone 2>/dev/null | grep -- ' -o alephone ')
if [ -z "$LINK_CMD" ]; then
  echo "Could not find the alephone link command in 'make -n -B' output" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
echo "Relinking for the browser into $OUT_DIR/alephone.js..."
(
  cd "$BUILD_DIR/Source_Files"
  eval "$LINK_CMD" \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createAlephOneModule \
    -sEXPORTED_RUNTIME_METHODS=FS,callMain \
    -sFORCE_FILESYSTEM=1 -sINVOKE_RUN=0 -sENVIRONMENT=web \
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1 \
    -o "$OUT_DIR/alephone.js"
)

echo "Wrote $OUT_DIR/alephone.js and $OUT_DIR/alephone.wasm"
