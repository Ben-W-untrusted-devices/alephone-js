# Web port (see ../../WEB_PORT_PLAN.md, M4d): identical to vcpkg's own
# community wasm32-emscripten triplet, except it adds -fwasm-exceptions
# -sSUPPORT_LONGJMP=wasm so freetype/libpng (pulled in transitively by
# sdl2-ttf) are ABI-compatible with the rest of this project's WASM
# exception-enabled build -- without this, wasm-ld fails with "undefined
# symbol: emscripten_longjmp" linking their prebuilt .a files. Only used to
# rebuild the specific ports that need it; everything else stays on the
# plain wasm32-emscripten triplet, since ports that don't use C++
# exceptions/setjmp/longjmp internally don't care about this ABI at all.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED EMSCRIPTEN_ROOT EMSDK PATH)

if(NOT DEFINED ENV{EMSCRIPTEN_ROOT})
   find_path(EMSCRIPTEN_ROOT "emcc")
else()
   set(EMSCRIPTEN_ROOT "$ENV{EMSCRIPTEN_ROOT}")
endif()

if(NOT EMSCRIPTEN_ROOT)
   if(NOT DEFINED ENV{EMSDK})
      message(FATAL_ERROR "The emcc compiler not found in PATH")
   endif()
   set(EMSCRIPTEN_ROOT "$ENV{EMSDK}/upstream/emscripten")
endif()

if(NOT EXISTS "${EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")
   message(FATAL_ERROR "Emscripten.cmake toolchain file not found")
endif()

set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
set(VCPKG_C_FLAGS "-fwasm-exceptions -sSUPPORT_LONGJMP=wasm")
set(VCPKG_CXX_FLAGS "-fwasm-exceptions -sSUPPORT_LONGJMP=wasm")
# Chainload vcpkg's wrapper toolchain rather than Emscripten.cmake directly:
# the wrapper includes Emscripten.cmake and then applies VCPKG_C(XX)_FLAGS
# and VCPKG_LINKER_FLAGS, which would otherwise be silently dropped.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${VCPKG_ROOT_DIR}/scripts/toolchains/emscripten.cmake")
