//#pragma once
//#include <string>
//#include <functional>
//
//class FileSystem {
//public:
//	/// <summary>
//	/// </summary>
//	/// <param name="path"></param>
//	/// <param name="is_direcroty"></param>
//	/// <returns></returns>
//	
//	static bool IsExist(std::string const& path, bool is_direcroty);
//
//	static bool IsFileExist(std::string const& path);
//	static bool IsDirExist(std::string const& path);
//
//	/// <summary>
//	/// </summary>
//	/// <returns></returns>
//	static std::string GetCurrentPath();
//
//	/// <summary>
//	/// </summary>
//	/// <param name="path"></param>
//	static void Enum(std::string const& path, bool EnumChild, std::function<bool(std::string const&)> callback);
//
//	static std::string parent_path(std::string const& path);
//
//	/// <summary>
//	/// </summary>
//	/// <param name="dir"></param>
//	/// <returns></returns>
//	static bool RemoveDir(std::string const& dir);
//	static bool RemoveFile(std::string const& file);
//
//	/// <summary>
//	/// </summary>
//	uintmax_t GetDirectorySize(std::string const& dir);
//
//	/// <summary>
//	/// </summary>
//	static bool create_directories(std::string const& path);
//};
