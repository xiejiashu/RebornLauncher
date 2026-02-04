#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct P2PSettings {
    bool enabled{ false };
    std::vector<std::string> stunServers;
    std::string signalEndpoint; // full http/https URL for signaling
};

// Lightweight WebRTC P2P helper built on libdatachannel.
class P2PClient {
public:
    P2PClient();

    void UpdateSettings(const P2PSettings& settings);
    P2PSettings GetSettings() const;

    // Attempt to fetch a file over WebRTC data channel. Returns true on success.
    // onProgress receives (downloaded bytes, total bytes if known).
    bool TryDownload(const std::string& url,
                     const std::string& filePath,
                     const std::function<void(uint64_t, uint64_t)>& onProgress);

private:
    struct ParsedEndpoint {
        bool useTls{ false };
        std::string host;
        std::string path{ "/signal" };
        int port{ 0 };
    };

    bool ParseEndpoint(const std::string& endpoint, ParsedEndpoint& parsed) const;

    P2PSettings m_settings;
    mutable std::mutex m_mutex;
};

