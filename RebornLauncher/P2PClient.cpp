#include "P2PClient.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <mutex>
#include <regex>
#include <string>
#include <system_error>
#include <vector>

namespace {

std::string NormalizeRelativePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.empty()) {
        return {};
    }
    if (path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    return path;
}

std::string NormalizeEndpointPath(std::string path) {
    if (path.empty()) {
        return "/signal";
    }
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    return path;
}

std::string UrlEncode(const std::string& input) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size() * 3);
    for (unsigned char ch : input) {
        const bool unreserved =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/';
        if (unreserved) {
            out.push_back(static_cast<char>(ch));
        }
        else {
            out.push_back('%');
            out.push_back(kHex[(ch >> 4) & 0x0F]);
            out.push_back(kHex[ch & 0x0F]);
        }
    }
    return out;
}

std::string BuildQueryPath(const std::string& basePath,
                           const std::string& key,
                           const std::string& value) {
    std::string out = basePath;
    out.append(basePath.find('?') == std::string::npos ? "?" : "&");
    out.append(key);
    out.push_back('=');
    out.append(UrlEncode(value));
    return out;
}

std::vector<std::string> BuildCandidatePaths(const std::string& endpointPath,
                                             const std::string& relativePath) {
    const std::string normalizedEndpoint = NormalizeEndpointPath(endpointPath);
    std::string relNoLeadingSlash = relativePath;
    if (!relNoLeadingSlash.empty() && relNoLeadingSlash.front() == '/') {
        relNoLeadingSlash.erase(relNoLeadingSlash.begin());
    }

    std::vector<std::string> candidates;
    candidates.reserve(4);
    candidates.push_back(BuildQueryPath(normalizedEndpoint, "url", relNoLeadingSlash));
    candidates.push_back(BuildQueryPath(normalizedEndpoint, "path", relNoLeadingSlash));
    candidates.push_back(BuildQueryPath(normalizedEndpoint, "page", relNoLeadingSlash));

    std::string appendedPath = normalizedEndpoint;
    if (!appendedPath.empty() && appendedPath.back() != '/') {
        appendedPath.push_back('/');
    }
    appendedPath.append(relNoLeadingSlash);
    candidates.push_back(std::move(appendedPath));
    return candidates;
}

bool IsSignalingJsonResponse(const httplib::Response& response) {
    std::string contentType = response.get_header_value("Content-Type");
    std::transform(contentType.begin(), contentType.end(), contentType.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return contentType.find("application/json") != std::string::npos
        || contentType.find("text/json") != std::string::npos;
}

bool ReplaceFile(const std::filesystem::path& src, const std::filesystem::path& dst) {
    std::error_code ec;
    std::filesystem::remove(dst, ec);
    ec.clear();
    std::filesystem::rename(src, dst, ec);
    if (!ec) {
        return true;
    }

    ec.clear();
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    std::filesystem::remove(src, ec);
    return true;
}

template <typename ClientType>
bool TryDownloadFromCandidates(ClientType& client,
                               const std::vector<std::string>& candidates,
                               const std::string& relativeUrl,
                               const std::string& filePath,
                               const std::function<void(uint64_t, uint64_t)>& onProgress) {
    client.set_follow_location(true);
    client.set_connection_timeout(8, 0);
    client.set_read_timeout(60, 0);
    client.set_write_timeout(15, 0);

    std::filesystem::path targetPath(filePath);
    std::filesystem::path tempPath = targetPath;
    tempPath += ".p2p.tmp";

    std::error_code ec;
    const auto parent = targetPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }

    std::string relNoLeadingSlash = relativeUrl;
    if (!relNoLeadingSlash.empty() && relNoLeadingSlash.front() == '/') {
        relNoLeadingSlash.erase(relNoLeadingSlash.begin());
    }

    const httplib::Headers headers = {
        { "X-UpdateForge-Relative-Path", relNoLeadingSlash },
        { "X-UpdateForge-Relative-Url", relativeUrl },
    };

    for (const auto& candidate : candidates) {
        std::filesystem::remove(tempPath, ec);
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }

        uint64_t downloaded = 0;
        uint64_t total = 0;
        auto res = client.Get(candidate.c_str(), headers,
                              [&](const char* data, size_t dataLength) {
                                  out.write(data, static_cast<std::streamsize>(dataLength));
                                  downloaded += static_cast<uint64_t>(dataLength);
                                  if (onProgress) {
                                      onProgress(downloaded, total);
                                  }
                                  return true;
                              });
        out.close();

        if (!res || (res->status != 200 && res->status != 206)) {
            std::filesystem::remove(tempPath, ec);
            continue;
        }

        if (IsSignalingJsonResponse(*res)) {
            std::filesystem::remove(tempPath, ec);
            continue;
        }

        const std::string contentLength = res->get_header_value("Content-Length");
        if (!contentLength.empty()) {
            try {
                total = static_cast<uint64_t>(std::stoull(contentLength));
            }
            catch (...) {
                total = downloaded;
            }
        }
        else {
            total = downloaded;
        }

        if (!ReplaceFile(tempPath, targetPath)) {
            std::filesystem::remove(tempPath, ec);
            continue;
        }

        if (onProgress) {
            onProgress(downloaded, total);
        }
        return true;
    }

    return false;
}

} // namespace

P2PClient::P2PClient() = default;

void P2PClient::UpdateSettings(const P2PSettings& settings) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_settings = settings;
}

P2PSettings P2PClient::GetSettings() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_settings;
}

bool P2PClient::ParseEndpoint(const std::string& endpoint, ParsedEndpoint& parsed) const {
    if (endpoint.empty()) {
        return false;
    }

    std::regex urlRegex(R"((https?)://([^/:]+)(?::(\d+))?(.*))");
    std::smatch match;
    if (!std::regex_match(endpoint, match, urlRegex)) {
        return false;
    }

    parsed.useTls = match[1].str() == "https";
    parsed.host = match[2].str();
    parsed.port = match[3].matched ? std::stoi(match[3].str()) : (parsed.useTls ? 443 : 80);

    std::string path = match[4].matched ? match[4].str() : "";
    if (path.empty()) {
        path = "/signal";
    }
    if (!path.empty() && path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    parsed.path = path;
    return true;
}

bool P2PClient::TryDownload(const std::string& url,
                            const std::string& filePath,
                            const std::function<void(uint64_t, uint64_t)>& onProgress) {
    const P2PSettings settings = GetSettings();
    if (!settings.enabled || settings.signalEndpoint.empty()) {
        return false;
    }

    ParsedEndpoint endpoint;
    if (!ParseEndpoint(settings.signalEndpoint, endpoint)) {
        return false;
    }

    const std::string relativeUrl = NormalizeRelativePath(url);
    if (relativeUrl.empty() || filePath.empty()) {
        return false;
    }

    const auto candidates = BuildCandidatePaths(endpoint.path, relativeUrl);
    if (candidates.empty()) {
        return false;
    }

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (endpoint.useTls) {
        httplib::SSLClient client(endpoint.host, endpoint.port);
        return TryDownloadFromCandidates(client, candidates, relativeUrl, filePath, onProgress);
    }
#endif

    if (endpoint.useTls) {
        return false;
    }

    httplib::Client client(endpoint.host, endpoint.port);
    return TryDownloadFromCandidates(client, candidates, relativeUrl, filePath, onProgress);
}
