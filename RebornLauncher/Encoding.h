#pragma once

#include <string>
#include <windows.h>

// Convert UTF-16 wide string to UTF-8 narrow string.
inline std::string wstr2str(const std::wstring& wstr) {
    if (wstr.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()),
                        result.data(), len, nullptr, nullptr);
    return result;
}

// Convert UTF-8 narrow string to UTF-16 wide string.
inline std::wstring str2wstr(const std::string& str, int strLen = -1) {
    int length = (strLen < 0) ? static_cast<int>(str.size()) : strLen;
    if (length <= 0) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), length, nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), length, result.data(), len);
    return result;
}

