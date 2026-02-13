#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <httplib.h>
#include <json/json.h>

namespace workthread::netutils {

std::string NormalizeRelativeUrlPath(std::string path);
std::string JoinUrlPath(const std::string& basePath, const std::string& childPath);
bool IsHttpUrl(const std::string& value);
bool ParseHttpUrl(const std::string& url, bool& useTls, std::string& host, int& port, std::string& path);
bool ExtractBaseAndPath(const std::string& absoluteUrl, std::string& baseUrl, std::string& path);
std::string TrimAscii(std::string value);
std::string DirnamePath(std::string path);
std::string GetFileNameFromUrl(std::string url);
bool HexBodyToBytes(const std::string& body, std::string& out);
uint64_t ParseTotalSizeFromResponse(const httplib::Response& response);
void MergeUnique(std::vector<std::string>& target, const std::vector<std::string>& source);
std::vector<std::string> ReadStringArray(const Json::Value& parent, const char* key);

} // namespace workthread::netutils
