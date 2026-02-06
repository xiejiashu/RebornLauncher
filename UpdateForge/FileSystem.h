#pragma once

#include <filesystem>
#include <functional>

class FileSystem {
public:
	/// <summary>
	/// </summary>
	/// <param name="path"></param>
	/// <param name="is_direcroty"></param>
	/// <returns></returns>
	static bool IsExist(std::filesystem::path const& path, bool is_direcroty);

	static bool IsFileExist(std::filesystem::path const& path);
	static bool IsDirExist(std::filesystem::path const& path);

	/// <summary>
	/// </summary>
	/// <returns></returns>
	static std::filesystem::path::string_type GetCurrentPath();

	/// <summary>
	/// </summary>
	/// <param name="path"></param>
	static void Enum(std::filesystem::path const& path, bool EnumChild, std::function<bool(std::filesystem::path const&)> callback);

	static std::string parent_path(std::string const& path);

	/// <summary>
	/// </summary>
	/// <param name="dir"></param>
	/// <returns></returns>
	static bool RemoveDir(std::string const& dir);
	static bool RemoveFile(std::string const& file);

	/// <summary>
	/// </summary>
	uintmax_t GetDirectorySize(std::string const& dir);

	/// <summary>
	/// </summary>
	static bool create_directories(std::filesystem::path const& path);
};
