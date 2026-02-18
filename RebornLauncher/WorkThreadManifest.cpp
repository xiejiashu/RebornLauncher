#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

#include <httplib.h>
#include <json/json.h>

#include "FileHash.h"
#include "WorkThreadNetUtils.h"

namespace {

constexpr const char* kBootstrapHost = "https://gitee.com";
#ifdef _DEBUG
constexpr const char* kBootstrapPath = "/MengMianHeiYiRen/MagicShow/raw/master/ReadMe.txt";
#else
constexpr const char* kBootstrapPath = "/MengMianHeiYiRen/MagicShow/raw/master/RemoteEncrypt.txt";
#endif

using workthread::netutils::DirnamePath;
using workthread::netutils::ExtractBaseAndPath;
using workthread::netutils::GetFileNameFromUrl;
using workthread::netutils::HexBodyToBytes;
using workthread::netutils::IsHttpUrl;
using workthread::netutils::JoinUrlPath;
using workthread::netutils::MergeUnique;
using workthread::netutils::NormalizeRelativeUrlPath;
using workthread::netutils::ParseHttpUrl;
using workthread::netutils::ReadStringArray;
using workthread::netutils::TrimAscii;

} // namespace

bool WorkThread::FetchBootstrapConfig()
{
	std::cout << "FetchBootstrapConfig" << std::endl;
	httplib::Client cli{ kBootstrapHost };
	auto res = cli.Get(kBootstrapPath);
	if (!res || res->status != 200) {
		if (res) {
			std::cout << "Failed to fetch bootstrap payload, status: " << res->status << std::endl;
		}
		else {
			std::cout << "Failed to fetch bootstrap payload, http error: " << res.error() << std::endl;
		}
		return false;
	}

	std::string ciphertext;
	if (!HexBodyToBytes(res->body, ciphertext)) {
		ciphertext = res->body;
	}
	const std::string decrypted = DecryptConfigPayload(ciphertext);

	Json::Value root;
	Json::Reader reader;
	if (!reader.parse(decrypted, root) || !root.isObject()) {
		std::cout << "Bootstrap payload is not valid JSON." << std::endl;
		return false;
	}

	Json::Value content = root["content"];
	if (!content.isObject()) {
		content = root["download"];
	}
	if (!content.isObject()) {
		std::cout << "Bootstrap JSON missing content/download object." << std::endl;
		return false;
	}

	std::string versionManifestUrl = TrimAscii(content["version_manifest_url"].asString());
	if (versionManifestUrl.empty()) {
		versionManifestUrl = TrimAscii(content["version_dat_url"].asString());
	}
	if (versionManifestUrl.empty()) {
		std::cout << "Bootstrap JSON missing version_manifest_url." << std::endl;
		return false;
	}

	std::string updateRootUrl = TrimAscii(content["update_package_root_url"].asString());
	if (updateRootUrl.empty()) {
		updateRootUrl = TrimAscii(content["runtime_root_url"].asString());
	}
	if (updateRootUrl.empty()) {
		updateRootUrl = TrimAscii(content["update_root_url"].asString());
	}

	m_versionState.basePackageUrls = ReadStringArray(content, "base_package_urls");
	if (m_versionState.basePackageUrls.empty()) {
		const std::string singleBasePackageUrl = TrimAscii(content["base_package_url"].asString());
		if (!singleBasePackageUrl.empty()) {
			m_versionState.basePackageUrls.push_back(singleBasePackageUrl);
		}
	}

	std::string versionBaseUrl;
	std::string versionPath;
	const bool versionManifestIsAbsolute = IsHttpUrl(versionManifestUrl);
	if (versionManifestIsAbsolute) {
		if (!ExtractBaseAndPath(versionManifestUrl, versionBaseUrl, versionPath)) {
			std::cout << "Invalid version_manifest_url: " << versionManifestUrl << std::endl;
			return false;
		}
		m_versionState.manifestPath = versionManifestUrl;
	}

	if (!updateRootUrl.empty() && IsHttpUrl(updateRootUrl)) {
		if (!ExtractBaseAndPath(updateRootUrl, m_networkState.url, m_networkState.page)) {
			std::cout << "Invalid update_package_root_url: " << updateRootUrl << std::endl;
			return false;
		}
	}
	else if (!updateRootUrl.empty()) {
		if (!versionBaseUrl.empty()) {
			m_networkState.url = versionBaseUrl;
		}
		else if (m_networkState.url.empty()) {
			std::cout << "Relative update_package_root_url requires absolute version_manifest_url." << std::endl;
			return false;
		}
		m_networkState.page = NormalizeRelativeUrlPath(updateRootUrl);
		if (m_networkState.page.back() != '/') {
			m_networkState.page.push_back('/');
		}
	}
	else if (!versionBaseUrl.empty()) {
		m_networkState.url = versionBaseUrl;
		m_networkState.page = DirnamePath(versionPath);
	}
	else {
		std::cout << "Bootstrap JSON cannot resolve download host/path." << std::endl;
		return false;
	}

	if (m_networkState.page.empty()) {
		m_networkState.page = "/";
	}
	if (m_networkState.page.front() != '/') {
		m_networkState.page.insert(m_networkState.page.begin(), '/');
	}
	if (m_networkState.page.back() != '/') {
		m_networkState.page.push_back('/');
	}
	if (!versionManifestIsAbsolute) {
		const bool rootedPath = !versionManifestUrl.empty() &&
			(versionManifestUrl.front() == '/' || versionManifestUrl.front() == '\\');
		if (rootedPath) {
			m_versionState.manifestPath = NormalizeRelativeUrlPath(versionManifestUrl);
		}
		else {
			m_versionState.manifestPath = JoinUrlPath(m_networkState.page, versionManifestUrl);
		}
	}
	if (m_versionState.manifestPath.empty()) {
		m_versionState.manifestPath = JoinUrlPath(m_networkState.page, "Version.dat");
	}

	if (m_versionState.basePackageUrls.empty()) {
		std::cout << "Bootstrap JSON missing base_package_urls/base_package_url." << std::endl;
		return false;
	}

	Json::Value p2pJson = root["p2p"];
	if (p2pJson.isObject()) {
		std::lock_guard<std::mutex> lock(m_networkState.p2pMutex);

		const std::string signalUrl = TrimAscii(p2pJson["signal_url"].asString());
		if (!signalUrl.empty() && m_networkState.p2pSettings.signalEndpoint.empty()) {
			m_networkState.p2pSettings.signalEndpoint = signalUrl;
		}

		const std::string signalToken = TrimAscii(p2pJson["signal_auth_token"].asString());
		if (!signalToken.empty() && m_networkState.p2pSettings.signalAuthToken.empty()) {
			m_networkState.p2pSettings.signalAuthToken = signalToken;
		}

		auto remoteStuns = ReadStringArray(p2pJson, "stun_servers");
		if (!remoteStuns.empty()) {
			MergeUnique(remoteStuns, m_networkState.p2pSettings.stunServers);
			m_networkState.p2pSettings.stunServers = std::move(remoteStuns);
		}
	}

	std::cout << "Bootstrap resolved: host=" << m_networkState.url
		<< " updateRoot=" << m_networkState.page
		<< " versionPath=" << m_versionState.manifestPath << std::endl;
	return true;
}

bool WorkThread::RefreshRemoteVersionManifest()
{
	const std::string strVersionDatPath = m_versionState.manifestPath.empty()
		? JoinUrlPath(m_networkState.page, "Version.dat")
		: m_versionState.manifestPath;
	constexpr int kManifestFetchMaxAttempts = 3;
	constexpr DWORD kManifestFetchRetryDelayMs = 250;

	auto fetchManifest = [this](const std::string& requestTarget) -> httplib::Result {
		httplib::Headers headers;
		headers.insert({ "Accept", "application/octet-stream" });
		headers.insert({ "Cache-Control", "no-cache" });

		if (IsHttpUrl(requestTarget)) {
			bool useTls = false;
			std::string host;
			int port = 0;
			std::string path;
			if (!ParseHttpUrl(requestTarget, useTls, host, port, path)) {
				return httplib::Result();
			}
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
			if (useTls) {
				httplib::SSLClient client(host, port);
				client.set_follow_location(true);
				client.set_connection_timeout(8, 0);
				client.set_read_timeout(60, 0);
				return client.Get(path.c_str(), headers);
			}
			else
#endif
			{
				httplib::Client client(host, port);
				client.set_follow_location(true);
				client.set_connection_timeout(8, 0);
				client.set_read_timeout(60, 0);
				return client.Get(path.c_str(), headers);
			}
		}

		if (!m_networkState.client) {
			return httplib::Result();
		}
		return m_networkState.client->Get(requestTarget.c_str(), headers);
	};

	auto fetchManifestWithRetry = [&](const std::string& requestTarget) -> httplib::Result {
		httplib::Result lastRes;
		for (int attempt = 1; attempt <= kManifestFetchMaxAttempts; ++attempt) {
			auto current = fetchManifest(requestTarget);
			if (current && current->status == 200 && !current->body.empty()) {
				return current;
			}
			lastRes = std::move(current);
			if (attempt < kManifestFetchMaxAttempts) {
				Sleep(kManifestFetchRetryDelayMs);
			}
		}
		return lastRes;
	};

	httplib::Result res = fetchManifestWithRetry(strVersionDatPath);
	std::string resolvedPath = strVersionDatPath;

	if ((!res || res->status != 200 || res->body.empty()) && !IsHttpUrl(strVersionDatPath)) {
		const std::string fileName = GetFileNameFromUrl(strVersionDatPath);
		const std::string fallbackPath = JoinUrlPath(m_networkState.page, fileName.empty() ? "Version.dat" : fileName);
		if (fallbackPath != strVersionDatPath) {
			httplib::Result fallbackRes = fetchManifestWithRetry(fallbackPath);
			if (fallbackRes && fallbackRes->status == 200 && !fallbackRes->body.empty()) {
				res = std::move(fallbackRes);
				resolvedPath = fallbackPath;
				m_versionState.manifestPath = fallbackPath;
			}
		}
	}

	if (!res || res->status != 200) {
		const std::string statusText = res ? std::to_string(res->status) : std::string("none");
		const int errorCode = static_cast<int>(res.error());
		std::cout << "Failed to fetch Version.dat from " << resolvedPath
			<< ", status: " << statusText
			<< ", error: " << errorCode
			<< ", retries: " << kManifestFetchMaxAttempts
			<< std::endl;
		return false;
	}
	if (res->body.empty()) {
		std::cout << "Version.dat response body is empty, path: " << resolvedPath << std::endl;
		return false;
	}

	auto parseJsonObject = [](const std::string& text, Json::Value& out) -> bool {
		Json::CharReaderBuilder builder;
		std::string errors;
		std::istringstream iss(text);
		return Json::parseFromStream(builder, iss, &out, &errors) && out.isObject();
	};

	std::string manifestBinary = res->body;
	std::string manifestJsonText;
	Json::Value root;

	if (parseJsonObject(manifestBinary, root)) {
		manifestJsonText = manifestBinary;
	}
	else {
		const std::string decompressed = DecryptVersionDat(manifestBinary);
		if (!decompressed.empty() && parseJsonObject(decompressed, root)) {
			manifestJsonText = decompressed;
		}
		else {
			std::string decoded;
			if (HexBodyToBytes(manifestBinary, decoded)) {
				if (parseJsonObject(decoded, root)) {
					manifestBinary = decoded;
					manifestJsonText = decoded;
				}
				else {
					const std::string decodedDecompressed = DecryptVersionDat(decoded);
					if (!decodedDecompressed.empty() && parseJsonObject(decodedDecompressed, root)) {
						manifestBinary = decoded;
						manifestJsonText = decodedDecompressed;
					}
				}
			}
		}
	}

	if (manifestJsonText.empty()) {
		std::cout << "Version.dat format not supported, path: " << resolvedPath
			<< ", body_size: " << res->body.size() << std::endl;
		return false;
	}

	WriteVersionToMapping(manifestJsonText);

	std::string strRemoteVersionDatMd5 = FileHash::string_md5(manifestBinary);
	if (strRemoteVersionDatMd5 != m_versionState.localVersionMD5)
	{
		std::ofstream ofs("Version.dat", std::ios::binary);
		ofs.write(manifestBinary.data(), manifestBinary.size());
		ofs.close();
		m_versionState.files.clear();
		Json::Value filesJson = root["file"];
		for (auto& fileJson : filesJson) {
			VersionConfig config;
			config.m_strMd5 = fileJson["md5"].asString();
			config.m_qwTime = fileJson["time"].asInt64();
			config.m_qwSize = fileJson["size"].asInt64();
			config.m_strPage = fileJson["page"].asString();
			if (config.m_strPage.empty()) {
				continue;
			}
			m_versionState.files[config.m_strPage] = config;
			try {
				const std::filesystem::path localPath =
					std::filesystem::current_path() / std::filesystem::u8path(config.m_strPage);
				std::error_code ec;
				const auto parent = localPath.parent_path();
				if (!parent.empty() && !std::filesystem::exists(parent, ec))
				{
					std::filesystem::create_directories(parent, ec);
				}
				ec.clear();
				if (!std::filesystem::exists(localPath, ec))
				{
					std::ofstream ofs(localPath, std::ios::binary);
					ofs.close();
				}
			}
			catch (...) {
				std::cout << "Skip invalid local page path: " << config.m_strPage << std::endl;
				m_versionState.files.erase(config.m_strPage);
			}
		}
		m_versionState.runtimeList.clear();
		Json::Value downloadList = root["runtime"];
		for (auto& download : downloadList) {
			m_versionState.runtimeList.push_back(download.asString());
		}
	}

	return true;
}
