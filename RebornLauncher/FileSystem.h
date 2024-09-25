#pragma once
#include <filesystem>
#include <functional>

class FileSystem {
public:
	/// <summary>
	/// 判断文件或目录是否存在
	/// </summary>
	/// <param name="path"></param>
	/// <param name="is_direcroty"></param>
	/// <returns></returns>
	static bool IsExist(std::filesystem::path const& path, bool is_direcroty);

	static bool IsFileExist(std::filesystem::path const& path);
	static bool IsDirExist(std::filesystem::path const& path);

	/// <summary>
	/// 进程当前工作目录
	/// </summary>
	/// <returns></returns>
	static std::filesystem::path::string_type GetCurrentPath();

	/// <summary>
	/// 遍历目录
	/// </summary>
	/// <param name="path"></param>
	/// <param name="EnumChild">是否递归遍历子目录</param>
	/// <param name="callback">返回false终止遍历</param>
	static void Enum(std::filesystem::path const& path, bool EnumChild, std::function<bool(std::filesystem::path const&)> callback);

	static std::string parent_path(std::string const& path);

	/// <summary>
	/// 递归删除目录
	/// </summary>
	/// <param name="dir"></param>
	/// <returns></returns>
	static bool RemoveDir(std::string const& dir);
	static bool RemoveFile(std::string const& file);

	/// <summary>
	/// 获取目录下文件的总大小
	/// </summary>
	uintmax_t GetDirectorySize(std::string const& dir);

	/// <summary>
	/// 递归创建目录
	/// </summary>
	static bool create_directories(std::filesystem::path const& path);
};
