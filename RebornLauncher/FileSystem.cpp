﻿//#include <iostream>
//#include "framework.h"
//#include "FileSystem.h"
//
//using namespace std;
//using namespace std::filesystem;
//
//bool FileSystem::IsExist(path const& path, bool is_direcroty)
//{
//    error_code error;
//    auto file_status = filesystem::status(path, error);
//    if (error) {
//        return false;
//    }
//    if (!filesystem::exists(file_status)) {
//        return false;
//    }
//    return is_direcroty && is_directory(file_status);
//}
//
//bool FileSystem::IsFileExist(path const& path)
//{
//    return IsExist(path, false);
//}
//
//bool FileSystem::IsDirExist(path const& path)
//{
//    return IsExist(path, true);
//}
//
//bool FileSystem::IsExist(std::string const& path, bool is_direcroty)
//{
//    return false;
//}
//
//bool FileSystem::IsFileExist(std::string const& path)
//{
//    return false;
//}
//
//bool FileSystem::IsDirExist(std::string const& path)
//{
//    return false;
//}
//
//path::string_type FileSystem::GetCurrentPath()
//{
//    using filesystem::current_path;
//    return current_path().native();
//}
//
//void FileSystem::Enum(path const& path, bool EnumChild, function<bool(filesystem::path const&)> callback)
//{
//    for (auto& p : directory_iterator(path)) {
//        if (!callback(p.path()))
//            break;
//        if (EnumChild && IsExist(p.path(), true)) {
//            Enum(p.path(), EnumChild, callback);
//        }
//    }
//}
//
//void FileSystem::Enum(std::string const& path, bool EnumChild, std::function<bool(std::string const&)> callback)
//{
//}
//
//std::string FileSystem::parent_path(string const& p)
//{
//    path path{p};
//    return path.parent_path().string();
//}
//
//bool FileSystem::RemoveDir(string const& dir)
//{
//    path p{ dir };
//
//    if (is_regular_file(p)) {
//        if (remove(p)) {
//            return true;
//        }
//    }
//
//    if (is_directory(p)) {
//        auto count = remove_all(p);
//        if (count > 0) {
//            return true;
//        }
//    }
//
//    return false;
//}
//
//bool FileSystem::RemoveFile(std::string const& file)
//{
//    path p{ file };
//
//    if (is_regular_file(p)) {
//        if (remove(p)) {
//            return true;
//        }
//    }
//
//    return false;
//}
//
//uintmax_t FileSystem::GetDirectorySize(string const& dir)
//{
//    error_code ec;
//    path p{ dir };
//    if (is_regular_file(p)) {
//        return file_size(p, ec);
//    }
//    uintmax_t totalSize = 0;
//    if (is_directory(p)) {
//        Enum(p, true, [&](path const& p) {
//            if (is_regular_file(p)) {
//                totalSize += file_size(p, ec);
//            }
//            return true;
//        });
//    }
//
//    return totalSize;
//}
//
//bool FileSystem::create_directories(std::string const& path)
//{
//    size_t pos = 0;
//    std::string currentPath;
//
//    while ((pos = path.find_first_of("\\/", pos)) != std::string::npos) {
//        currentPath = path.substr(0, pos++);
//        if (!currentPath.empty() && !CreateDirectory(currentPath.c_str(), NULL)) {
//            if (GetLastError() != ERROR_ALREADY_EXISTS) {
//                std::cout << "Failed to create directory: " << currentPath << std::endl;
//                return false;
//            }
//        }
//    }
//
//    if (!CreateDirectory(path.c_str(), NULL)) {
//        if (GetLastError() != ERROR_ALREADY_EXISTS) {
//            std::cerr << "Failed to create directory: " << path << std::endl;
//            return false;
//        }
//    }
//
//    return true;
//}