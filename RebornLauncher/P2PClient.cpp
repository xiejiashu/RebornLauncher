#include "P2PClient.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <httplib.h>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <rtc/rtc.hpp>
#include <sstream>
#include <variant>

namespace {
std::unique_ptr<httplib::Client> MakeSignalClient(const P2PClient::ParsedEndpoint& endpoint) {
    if (endpoint.host.empty()) {
        return nullptr;
    }

    std::unique_ptr<httplib::Client> client;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (endpoint.useTls) {
        client = std::make_unique<httplib::SSLClient>(endpoint.host, endpoint.port);
    }
#endif
    if (!endpoint.useTls) {
        client = std::make_unique<httplib::Client>(endpoint.host, endpoint.port);
    }

    if (client) {
        client->set_connection_timeout(5, 0);
        client->set_read_timeout(10, 0);
    }
    return client;
}
}  // namespace

P2PClient::P2PClient() {
    rtc::InitLogger(rtc::LogLevel::Warning);
}

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
    if (!settings.enabled || settings.stunServers.empty()) {
        return false;
    }

    ParsedEndpoint endpoint;
    if (!ParseEndpoint(settings.signalEndpoint, endpoint)) {
        return false;
    }

    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    rtc::Configuration config;
    for (const auto& stun : settings.stunServers) {
        rtc::IceServer server;
        server.urls.push_back(stun);
        config.iceServers.push_back(server);
    }

    auto peerConnection = std::make_shared<rtc::PeerConnection>(config);
    std::vector<rtc::Candidate> localCandidates;
    std::mutex candidateMutex;
    std::condition_variable cv;
    std::atomic_bool handshakeFailed{ false };
    std::atomic_bool downloadSucceeded{ false };

    std::optional<rtc::Description> localDescription;

    peerConnection->onStateChange([&](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Failed ||
            state == rtc::PeerConnection::State::Disconnected ||
            state == rtc::PeerConnection::State::Closed) {
            handshakeFailed = true;
            cv.notify_all();
        }
    });

    peerConnection->onLocalDescription([&](rtc::Description description) {
        localDescription = description;
    });

    peerConnection->onLocalCandidate([&](rtc::Candidate candidate) {
        std::lock_guard<std::mutex> lock(candidateMutex);
        localCandidates.push_back(candidate);
    });

    uint64_t expectedSize = 0;
    uint64_t receivedSize = 0;

    auto dataChannel = peerConnection->createDataChannel("p2p-download");
    dataChannel->onOpen([dataChannel, url]() {
        dataChannel->send(std::string("GET ") + url);
    });

    dataChannel->onMessage([&](rtc::message_variant message) {
        if (auto text = std::get_if<std::string>(&message)) {
            if (text->rfind("SIZE ", 0) == 0) {
                expectedSize = std::strtoull(text->substr(5).c_str(), nullptr, 10);
                if (onProgress) {
                    onProgress(receivedSize, expectedSize);
                }
            }
        } else if (auto binary = std::get_if<rtc::binary>(&message)) {
            output.write(reinterpret_cast<const char*>(binary->data()),
                         static_cast<std::streamsize>(binary->size()));
            receivedSize += binary->size();
            if (onProgress) {
                onProgress(receivedSize, expectedSize);
            }
            if (expectedSize > 0 && receivedSize >= expectedSize) {
                downloadSucceeded = true;
                cv.notify_all();
            }
        }
    });

    dataChannel->onClosed([&]() {
        if (expectedSize == 0 || receivedSize >= expectedSize) {
            downloadSucceeded = true;
        }
        cv.notify_all();
    });

    std::atomic_bool offerSent{ false };
    peerConnection->onGatheringStateChange([&](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            offerSent = true;
            cv.notify_all();
        }
    });

    // Trigger offer creation.
    peerConnection->setLocalDescription();

    // Wait until ICE gathering completes or fails.
    {
        std::mutex waitMutex;
        std::unique_lock<std::mutex> lock(waitMutex);
        cv.wait_for(lock, std::chrono::seconds(3), [&]() {
            return offerSent.load() || handshakeFailed.load();
        });
    }

    if (!localDescription.has_value() || handshakeFailed.load()) {
        return false;
    }

    // Build offer payload.
    Json::Value payload;
    payload["resource"] = url;
    payload["sdp"] = localDescription->sdp();
    payload["type"] = localDescription->typeString();

    {
        Json::Value candidates(Json::arrayValue);
        std::lock_guard<std::mutex> lock(candidateMutex);
        for (const auto& cand : localCandidates) {
            Json::Value item;
            item["candidate"] = cand.candidate();
            item["mid"] = cand.mid();
            candidates.append(item);
        }
        payload["candidates"] = candidates;
    }

    auto client = MakeSignalClient(endpoint);
    if (!client) {
        return false;
    }

    Json::StreamWriterBuilder wb;
    wb["omitEndingLineFeed"] = true;
    const std::string body = Json::writeString(wb, payload);
    const std::string path = endpoint.path.empty() ? "/signal" : endpoint.path;

    auto response = client->Post(path.c_str(), body, "application/json");
    if (!response || response->status != 200) {
        return false;
    }

    Json::CharReaderBuilder rb;
    Json::Value answer;
    std::string errs;
    std::istringstream iss(response->body);
    if (!Json::parseFromStream(rb, iss, &answer, &errs)) {
        return false;
    }

    try {
        const std::string answerSdp = answer["sdp"].asString();
        const std::string answerType = answer.get("type", "answer").asString();
        peerConnection->setRemoteDescription(rtc::Description(answerSdp, answerType));

        if (answer.isMember("candidates")) {
            for (const auto& cand : answer["candidates"]) {
                rtc::Candidate remoteCand(cand["candidate"].asString(),
                                          cand.get("mid", "").asString());
                peerConnection->addRemoteCandidate(remoteCand);
            }
        }
    } catch (const std::exception&) {
        return false;
    }

    // Wait for completion or timeout.
    {
        std::mutex waitMutex;
        std::unique_lock<std::mutex> lock(waitMutex);
        cv.wait_for(lock, std::chrono::seconds(20), [&]() {
            return downloadSucceeded.load() || handshakeFailed.load();
        });
    }

    return downloadSucceeded.load();
}
