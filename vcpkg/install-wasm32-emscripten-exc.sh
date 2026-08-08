# Web port (see ../../WEB_PORT_PLAN.md, M4d): freetype and libpng (pulled
# in transitively by sdl2-ttf, installed by install-wasm32-emscripten.sh)
# use setjmp/longjmp internally for their own error handling. Once
# configure.ac started passing -fwasm-exceptions -sSUPPORT_LONGJMP=wasm for
# the Emscripten target (this project's own code needs real, working C++
# exception handling -- see configure.ac), their plain wasm32-emscripten
# builds became ABI-incompatible with the rest of the link (wasm-ld:
# "undefined symbol: emscripten_longjmp"). Rebuilds just those two ports
# under a custom triplet with matching flags
# (custom-triplets/wasm32-emscripten-exc.cmake), then copies the resulting
# .a files over the incompatible ones already installed under the plain
# wasm32-emscripten triplet -- everything else (sdl2, sdl2-ttf itself,
# openal-soft, libsndfile) doesn't use C++ exceptions/setjmp/longjmp
# internally, so it doesn't need rebuilding or care which freetype/libpng
# it links against.
#
# Run from this directory (vcpkg/), after install-wasm32-emscripten.sh.
set -e

MAIN_LIB=installed-wasm32-emscripten/wasm32-emscripten/lib
EXC_LIB=installed-wasm32-emscripten-exc/wasm32-emscripten-exc/lib

if [ ! -f "$MAIN_LIB/libfreetype.a" ] || [ ! -f "$MAIN_LIB/libpng16.a" ]; then
  echo "$MAIN_LIB doesn't have libfreetype.a/libpng16.a yet -- run install-wasm32-emscripten.sh first" >&2
  exit 1
fi

`cat ~/.vcpkg/vcpkg.path.txt`/vcpkg --classic --overlay-triplets=custom-triplets \
  --triplet=wasm32-emscripten-exc --x-install-root=installed-wasm32-emscripten-exc \
  --feature-flags="versions" install freetype libpng

cp "$EXC_LIB/libfreetype.a" "$MAIN_LIB/libfreetype.a"
cp "$EXC_LIB/libpng16.a" "$MAIN_LIB/libpng16.a"

echo "Copied exception-compatible libfreetype.a/libpng16.a into $MAIN_LIB"
