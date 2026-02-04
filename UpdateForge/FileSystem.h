#pragma once

#include <filesystem>
#include <functional>

class FileSystem {
public:
	/// <summary>
	/// 鍒ゆ柇鏂囦欢鎴栫洰褰曟槸鍚﹀瓨鍦?
	/// </summary>
	/// <param name="path"></param>
	/// <param name="is_direcroty"></param>
	/// <returns></returns>
	static bool IsExist(std::filesystem::path const& path, bool is_direcroty);

	static bool IsFileExist(std::filesystem::path const& path);
	static bool IsDirExist(std::filesystem::path const& path);

	/// <summary>
	/// 杩涚▼褰撳墠宸ヤ綔鐩綍
	/// </summary>
	/// <returns></returns>
	static std::filesystem::path::string_type GetCurrentPath();

	/// <summary>
	/// 閬嶅巻鐩綍
	/// </summary>
	/// <param name="path"></param>
	/// <param name="EnumChild">鏄惁閫掑綊閬嶅巻瀛愮洰褰?/param>
	/// <param name="callback">杩斿洖false缁堟閬嶅巻</param>
	static void Enum(std::filesystem::path const& path, bool EnumChild, std::function<bool(std::filesystem::path const&)> callback);

	static std::string parent_path(std::string const& path);

	/// <summary>
	/// 閫掑綊鍒犻櫎鐩綍
	/// </summary>
	/// <param name="dir"></param>
	/// <returns></returns>
	static bool RemoveDir(std::string const& dir);
	static bool RemoveFile(std::string const& file);

	/// <summary>
	/// 鑾峰彇鐩綍涓嬫枃浠剁殑鎬诲ぇ灏?
	/// </summary>
	uintmax_t GetDirectorySize(std::string const& dir);

	/// <summary>
	/// 閫掑綊鍒涘缓鐩綍
	/// </summary>
	static bool create_directories(std::filesystem::path const& path);
};
