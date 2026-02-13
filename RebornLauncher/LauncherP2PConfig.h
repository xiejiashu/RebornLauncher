#pragma once

#include <filesystem>
#include <string>
#include <vector>

std::wstring TrimWide(const std::wstring& value);
std::string TrimAscii(std::string value);

std::string ReadEnvVarTrimmed(const char* name);
std::string ReadOptionalTextFileTrimmed(const std::filesystem::path& path);

std::vector<std::string> ParseStunServerList(const std::string& raw);
std::vector<std::wstring> LoadStunServersFromFile(const std::filesystem::path& path);
bool SaveStunServersToFile(const std::filesystem::path& path, const std::vector<std::wstring>& servers);

std::vector<std::wstring> DefaultStunServersWide();
std::vector<std::string> BuildMergedStunServers(const std::vector<std::wstring>& uiServers, const std::string& envRaw);

std::string ResolveSignalEndpoint(const std::filesystem::path& workDir);
std::string ResolveSignalAuthToken(const std::filesystem::path& workDir);
