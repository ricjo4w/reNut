#pragma once
// Where reNut writes its own files (renut.toml, logs, pipeline_list.bin, the
// path setup, shader captures). Next to the executable, as the SDK assumes;
// an AppImage runs from a read-only mount, so there it is the user data
// folder the SDK keeps saves in ($XDG_DATA_HOME/renut, or
// ~/.local/share/renut), and the working directory is moved there so the
// paths reNut writes relative to it land there too.

#include <cstdlib>
#include <filesystem>
#include <system_error>

#include <rex/filesystem.h>

namespace renut::paths {

// The AppImage runtime sets APPIMAGE to the image's own path.
inline bool IsAppImage() {
#if defined(__linux__)
	const char *image = std::getenv("APPIMAGE");
	return image && *image;
#else
	return false;
#endif
}

inline bool UsesUserFolder() {
#if defined(__APPLE__)
	return true;
#else
	return IsAppImage();
#endif
}

inline std::filesystem::path WritableFolder() {
#if defined(__APPLE__)
	if (const char *home = std::getenv("HOME"); home && *home)
		return std::filesystem::path(home) / "Library/Application Support/renut";
#endif
	if (UsesUserFolder())
		return rex::filesystem::GetUserFolder() / "renut";
	return rex::filesystem::GetExecutableFolder();
}

// Once, before anything is written: creates the folder and, for an AppImage,
// makes it the working directory.
inline void PrepareWritableFolder() {
	if (!UsesUserFolder())
		return;
	std::error_code ec;
	const std::filesystem::path folder = WritableFolder();
	std::filesystem::create_directories(folder, ec);
	std::filesystem::current_path(folder, ec);
}

} // namespace renut::paths
