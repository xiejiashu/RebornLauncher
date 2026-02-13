#include "framework.h"
#include "LauncherP2PConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

#include "Encoding.h"

namespace {

constexpr const char* kSignalEndpointFile = "p2p_signal_endpoint.txt";
constexpr const char* kSignalAuthTokenFile = "p2p_signal_auth_token.txt";

std::vector<std::wstring> DefaultStunServersWideInternal() {
    return {
        L"stun:stun.l.google.com:19302",
        L"stun:global.stun.twilio.com:3478",
        L"stun:stun.cloudflare.com:3478",
    };
}

void MergeUniqueKeepOrder(std::vector<std::string>& target, const std::vector<std::string>& source) {
    for (const auto& value : source) {
        if (value.empty()) {
            continue;
        }
        if (std::find(target.begin(), target.end(), value) == target.end()) {
            target.push_back(value);
        }
    }
}

std::string ResolveByFileThenEnv(const std::filesystem::path& workDir,
                                 const char* fileName,
                                 const char* primaryEnv,
                                 const char* fallbackEnv) {
    std::string value = ReadOptionalTextFileTrimmed(workDir / fileName);
    if (value.empty()) {
        value = ReadEnvVarTrimmed(primaryEnv);
    }
    if (value.empty()) {
        value = ReadEnvVarTrimmed(fallbackEnv);
    }
    return value;
}

} // namespace

std::wstring TrimWide(const std::wstring& value) {
    constexpr wchar_t whitespace[] = L" \t\r\n";
    const auto start = value.find_first_not_of(whitespace);
    if (start == std::wstring::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

std::string TrimAscii(std::string value) {
    constexpr const char* whitespace = " \t\r\n";
    const auto start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

std::string ReadEnvVarTrimmed(const char* name) {
    char* raw = nullptr;
    size_t len = 0;
    if (_dupenv_s(&raw, &len, name) != 0 || raw == nullptr) {
        return {};
    }
    std::string value(raw);
    free(raw);
    return TrimAscii(value);
}

std::string ReadOptionalTextFileTrimmed(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return TrimAscii(content);
}

std::vector<std::string> ParseStunServerList(const std::string& raw) {
    std::string normalized = raw;
    for (char& ch : normalized) {
        if (ch == ',' || ch == ';' || ch == '\r' || ch == '\t') {
            ch = '\n';
        }
    }

    std::vector<std::string> result;
    std::istringstream in(normalized);
    std::string line;
    while (std::getline(in, line)) {
        std::string value = TrimAscii(line);
        if (value.empty()) {
            continue;
        }
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower.rfind("stun:", 0) != 0 && lower.rfind("turn:", 0) != 0) {
            value = "stun:" + value;
        }
        if (std::find(result.begin(), result.end(), value) == result.end()) {
            result.push_back(value);
        }
    }
    return result;
}

std::vector<std::wstring> LoadStunServersFromFile(const std::filesystem::path& path) {
    std::vector<std::wstring> servers;
    if (std::filesystem::exists(path)) {
        std::ifstream in(path, std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            std::wstring value = TrimWide(str2wstr(line, static_cast<int>(line.size())));
            if (!value.empty()) {
                servers.push_back(value);
            }
        }
    }
    if (servers.empty()) {
        servers = DefaultStunServersWideInternal();
    }
    return servers;
}

bool SaveStunServersToFile(const std::filesystem::path& path, const std::vector<std::wstring>& servers) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    for (const auto& server : servers) {
        out << wstr2str(server) << "\n";
    }
    return out.good();
}

std::vector<std::wstring> DefaultStunServersWide() {
    return DefaultStunServersWideInternal();
}

std::vector<std::string> BuildMergedStunServers(const std::vector<std::wstring>& uiServers, const std::string& envRaw) {
    std::vector<std::string> fromUi;
    fromUi.reserve(uiServers.size());
    for (const auto& server : uiServers) {
        std::string value = wstr2str(server);
        if (!value.empty()) {
            fromUi.push_back(value);
        }
    }
    if (fromUi.empty()) {
        for (const auto& server : DefaultStunServersWideInternal()) {
            fromUi.push_back(wstr2str(server));
        }
    }

    const std::vector<std::string> fromEnv = ParseStunServerList(envRaw);
    if (fromEnv.empty()) {
        return fromUi;
    }

    std::vector<std::string> merged;
    merged.reserve(fromEnv.size() + fromUi.size());
    MergeUniqueKeepOrder(merged, fromEnv);
    MergeUniqueKeepOrder(merged, fromUi);
    return merged;
}

std::string ResolveSignalEndpoint(const std::filesystem::path& workDir) {
    return ResolveByFileThenEnv(workDir, kSignalEndpointFile, "P2P_SIGNAL_ENDPOINT", "SIGNAL_ENDPOINT");
}

std::string ResolveSignalAuthToken(const std::filesystem::path& workDir) {
    return ResolveByFileThenEnv(workDir, kSignalAuthTokenFile, "P2P_SIGNAL_AUTH_TOKEN", "SIGNAL_AUTH_TOKEN");
}
