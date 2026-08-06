#ifndef __PORTABLE_FILESYSTEM_H
#define __PORTABLE_FILESYSTEM_H

/*
	portable_filesystem.h

	Web-port addition (see ../../WEB_PORT_PLAN.md, M3): aliases std::filesystem
	instead of boost::filesystem when targeting Emscripten. The rest of the
	codebase uses boost::filesystem because std::filesystem isn't available
	before macOS 10.15 (see Source_Files/XML/Plugins.cpp), which isn't a
	constraint an Emscripten build has -- and cross-compiling a real
	libboost_filesystem for wasm32 is a much larger, more fragile undertaking
	than this alias. Native builds are unaffected; they still get
	boost::filesystem exactly as before.

	aone_sys aliases the whole boost::system/std namespace, not just
	error_code: std::error_code and std::generic_category() live directly in
	namespace std (there's no nested std::system), so this lets call sites
	that write "sys::error_code ec" or "sys::generic_category()" keep working
	unchanged under both namespace bindings.
*/

#ifdef __EMSCRIPTEN__
#include <filesystem>
#include <system_error>
namespace aone_fs = std::filesystem;
namespace aone_sys = std;
#else
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
namespace aone_fs = boost::filesystem;
namespace aone_sys = boost::system;
#endif

#endif
