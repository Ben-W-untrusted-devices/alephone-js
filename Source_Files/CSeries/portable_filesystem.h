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

	A few boost::filesystem APIs this codebase uses have no std::filesystem
	equivalent, or use differently-named enumerators, so those get small
	portable wrappers below rather than a plain namespace alias:
	- aone_fs_regular_file / aone_fs_directory_file: boost::filesystem::
	  file_type's enumerators are "regular_file"/"directory_file";
	  std::filesystem::file_type's are "regular"/"directory".
	- aone_fs_unique_path(model): boost::filesystem::unique_path() has no
	  std::filesystem equivalent at all; the Emscripten branch reimplements
	  its "%" -> random hex digit substitution.
	- aone_fs_file_time_to_time_t(t): boost's file_time_type is time_t-
	  compatible; std::filesystem::file_time_type is a chrono::time_point on
	  an unspecified clock, so converting to time_t needs an explicit (C++17-
	  compatible, pre-clock_cast) clock conversion.
*/

#ifdef __EMSCRIPTEN__
#include <chrono>
#include <ctime>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
namespace aone_fs = std::filesystem;
namespace aone_sys = std;

constexpr aone_fs::file_type aone_fs_regular_file = aone_fs::file_type::regular;
constexpr aone_fs::file_type aone_fs_directory_file = aone_fs::file_type::directory;

inline std::string aone_fs_unique_path(const std::string& model)
{
	static const char hex_digits[] = "0123456789abcdef";
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> dist(0, 15);
	std::string result = model;
	for (auto& c : result) {
		if (c == '%') c = hex_digits[dist(gen)];
	}
	return result;
}

inline std::time_t aone_fs_file_time_to_time_t(const aone_fs::file_time_type& t)
{
	const auto system_time = std::chrono::system_clock::now() +
		std::chrono::duration_cast<std::chrono::system_clock::duration>(
			t - aone_fs::file_time_type::clock::now());
	return std::chrono::system_clock::to_time_t(system_time);
}
#else
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
namespace aone_fs = boost::filesystem;
namespace aone_sys = boost::system;

constexpr aone_fs::file_type aone_fs_regular_file = aone_fs::file_type::regular_file;
constexpr aone_fs::file_type aone_fs_directory_file = aone_fs::file_type::directory_file;

inline std::string aone_fs_unique_path(const std::string& model)
{
	return boost::filesystem::unique_path(model).string();
}

// boost::filesystem::last_write_time() returns std::time_t directly -- there
// is no boost::filesystem::file_time_type.
inline std::time_t aone_fs_file_time_to_time_t(std::time_t t)
{
	return t;
}
#endif

#endif
