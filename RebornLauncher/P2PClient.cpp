#include "P2PClient.h"

#include <mutex>
#include <regex>

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
    if (!settings.enabled) {
        return false;
    }
    // Stubbed P2P: return false to fall back to HTTP for now.
    (void)url;
    (void)filePath;
    if (onProgress) {
        onProgress(0, 0);
    }
    return false;
}
