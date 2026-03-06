#include "framework.h"
#include "LauncherUpdateCoordinator.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <httplib.h>
#include <json/json.h>

#include "FileHash.h"
#include "NetUtils.h"

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

std::string ToLowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
		if (ch >= 'A' && ch <= 'Z') {
			return static_cast<char>(ch - 'A' + 'a');
		}
		return static_cast<char>(ch);
	});
	return value;
}

std::string NormalizeManifestPathKey(std::string path) {
	path = TrimAscii(std::move(path));
	if (path.empty()) {
		return {};
	}
	std::replace(path.begin(), path.end(), '\\', '/');
	while (path.rfind("./", 0) == 0) {
		path.erase(0, 2);
	}
	while (!path.empty() && path.front() == '/') {
		path.erase(path.begin());
	}
	if (path.empty()) {
		return {};
	}

	try {
		std::filesystem::path normalized = std::filesystem::u8path(path).lexically_normal();
		std::string key = normalized.generic_string();
		while (key.rfind("./", 0) == 0) {
			key.erase(0, 2);
		}
		while (!key.empty() && key.front() == '/') {
			key.erase(key.begin());
		}
		std::replace(key.begin(), key.end(), '/', '\\');
		return ToLowerAscii(std::move(key));
	}
	catch (...) {
		return {};
	}
}

std::filesystem::path BuildLocalFilePathFromManifestPath(const std::string& manifestPath) {
	std::string keyPath = NormalizeManifestPathKey(manifestPath);
	if (keyPath.empty()) {
		return {};
	}
	std::replace(keyPath.begin(), keyPath.end(), '\\', '/');
	return std::filesystem::u8path(keyPath);
}

bool DoesLocalFileMatchRemoteSize(const std::string& manifestPath, const VersionConfig& remoteConfig) {
	const std::filesystem::path localPath = BuildLocalFilePathFromManifestPath(manifestPath);
	if (localPath.empty()) {
		return false;
	}

	std::error_code ec;
	if (!std::filesystem::exists(localPath, ec) || ec) {
		return false;
	}

	if (remoteConfig.m_qwSize > 0) {
		const uint64_t localSize = std::filesystem::file_size(localPath, ec);
		if (ec || localSize != static_cast<uint64_t>(remoteConfig.m_qwSize)) {
			return false;
		}
	}
	return true;
}

std::unordered_map<std::string, const VersionConfig*> BuildManifestLookupByNormalizedKey(
	const std::map<std::string, VersionConfig>& files) {
	std::unordered_map<std::string, const VersionConfig*> lookup;
	lookup.reserve(files.size());
	for (const auto& [path, cfg] : files) {
		const std::string key = NormalizeManifestPathKey(path);
		if (!key.empty()) {
			lookup[key] = &cfg;
		}
	}
	return lookup;
}

bool IsManifestEntryDifferent(const VersionConfig& localConfig, const VersionConfig& remoteConfig) {
	const std::string localMd5 = ToLowerAscii(TrimAscii(localConfig.m_strMd5));
	const std::string remoteMd5 = ToLowerAscii(TrimAscii(remoteConfig.m_strMd5));
	if (!localMd5.empty() && !remoteMd5.empty()) {
		return localMd5 != remoteMd5;
	}
	if (!localMd5.empty() || !remoteMd5.empty()) {
		return true;
	}
	return localConfig.m_qwTime != remoteConfig.m_qwTime ||
		localConfig.m_qwSize != remoteConfig.m_qwSize;
}

std::vector<std::string> BuildPrelaunchSyncList(
	const std::map<std::string, VersionConfig>& localFiles,
	const std::map<std::string, VersionConfig>& remoteFiles,
	bool hasLocalManifestBaseline,
	size_t& manifestDiffCount,
	size_t& sizeValidationDiffCount) {
	manifestDiffCount = 0;
	sizeValidationDiffCount = 0;

	std::vector<std::string> syncList;
	syncList.reserve(remoteFiles.size());
	std::unordered_set<std::string> insertedKeys;
	insertedKeys.reserve(remoteFiles.size());

	const auto localLookup = hasLocalManifestBaseline
		? BuildManifestLookupByNormalizedKey(localFiles)
		: std::unordered_map<std::string, const VersionConfig*>{};

	for (const auto& [remotePath, remoteConfig] : remoteFiles) {
		const std::string key = NormalizeManifestPathKey(remotePath);
		if (key.empty()) {
			continue;
		}

		bool needsSync = false;
		if (hasLocalManifestBaseline) {
			const auto localIt = localLookup.find(key);
			if (localIt == localLookup.end() || localIt->second == nullptr) {
				needsSync = true;
			}
			else if (!DoesLocalFileMatchRemoteSize(remotePath, remoteConfig)) {
				needsSync = true;
			}
			else {
				needsSync = IsManifestEntryDifferent(*localIt->second, remoteConfig);
			}
			if (needsSync) {
				++manifestDiffCount;
			}
		}
		else {
			needsSync = !DoesLocalFileMatchRemoteSize(remotePath, remoteConfig);
			if (needsSync) {
				++sizeValidationDiffCount;
			}
		}

		if (needsSync && insertedKeys.insert(key).second) {
			syncList.push_back(remotePath);
		}
	}

	return syncList;
}

} // namespace

bool LauncherUpdateCoordinator::FetchBootstrapConfig()
{
	SetLauncherStatus(L"Fetching bootstrap configuration...");
	std::cout << "FetchBootstrapConfig" << std::endl;
	httplib::Client cli{ kBootstrapHost };
	auto res = cli.Get(kBootstrapPath);
	if (!res || res->status != 200) {
		SetLauncherStatus(L"Failed: bootstrap configuration request.");
		if (res) {
			std::cout << "Failed to fetch bootstrap payload, status: " << res->status << std::endl;
			LogUpdateError(
				"UF-BOOTSTRAP-HTTP",
				"LauncherUpdateCoordinator::FetchBootstrapConfig",
				"Bootstrap HTTP request returned non-200 status",
				std::string("host=") + kBootstrapHost + ", path=" + kBootstrapPath,
				0,
				res->status,
				0);
		}
		else {
			std::cout << "Failed to fetch bootstrap payload, http error: " << res.error() << std::endl;
			LogUpdateError(
				"UF-BOOTSTRAP-HTTP",
				"LauncherUpdateCoordinator::FetchBootstrapConfig",
				"Bootstrap HTTP request failed",
				std::string("host=") + kBootstrapHost + ", path=" + kBootstrapPath,
				0,
				0,
				static_cast<int>(res.error()));
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
		SetLauncherStatus(L"Failed: invalid bootstrap configuration.");
		LogUpdateError(
			"UF-BOOTSTRAP-JSON",
			"LauncherUpdateCoordinator::FetchBootstrapConfig",
			"Bootstrap payload parse failed",
			"Decrypted bootstrap content is not a valid JSON object.");
		return false;
	}

	Json::Value content = root["content"];
	if (!content.isObject()) {
		content = root["download"];
	}
	if (!content.isObject()) {
		std::cout << "Bootstrap JSON missing content/download object." << std::endl;
		SetLauncherStatus(L"Failed: malformed bootstrap content.");
		LogUpdateError(
			"UF-BOOTSTRAP-FIELD",
			"LauncherUpdateCoordinator::FetchBootstrapConfig",
			"Bootstrap JSON missing content/download object",
			"Expected root.content or root.download object.");
		return false;
	}

	std::string versionManifestUrl = TrimAscii(content["version_manifest_url"].asString());
	if (versionManifestUrl.empty()) {
		versionManifestUrl = TrimAscii(content["version_dat_url"].asString());
	}
	if (versionManifestUrl.empty()) {
		std::cout << "Bootstrap JSON missing version_manifest_url." << std::endl;
		SetLauncherStatus(L"Failed: missing manifest URL in bootstrap.");
		LogUpdateError(
			"UF-BOOTSTRAP-FIELD",
			"LauncherUpdateCoordinator::FetchBootstrapConfig",
			"Bootstrap JSON missing version manifest URL",
			"Neither version_manifest_url nor version_dat_url is present.");
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
			SetLauncherStatus(L"Failed: invalid manifest URL.");
			LogUpdateError(
				"UF-BOOTSTRAP-URL",
				"LauncherUpdateCoordinator::FetchBootstrapConfig",
				"Invalid version manifest URL format",
				std::string("version_manifest_url=") + versionManifestUrl);
			return false;
		}
		m_versionState.manifestPath = versionManifestUrl;
	}

	if (!updateRootUrl.empty() && IsHttpUrl(updateRootUrl)) {
		if (!ExtractBaseAndPath(updateRootUrl, m_networkState.url, m_networkState.page)) {
			std::cout << "Invalid update_package_root_url: " << updateRootUrl << std::endl;
			SetLauncherStatus(L"Failed: invalid update root URL.");
			LogUpdateError(
				"UF-BOOTSTRAP-URL",
				"LauncherUpdateCoordinator::FetchBootstrapConfig",
				"Invalid update root URL format",
				std::string("update_package_root_url=") + updateRootUrl);
			return false;
		}
	}
	else if (!updateRootUrl.empty()) {
		if (!versionBaseUrl.empty()) {
			m_networkState.url = versionBaseUrl;
		}
		else if (m_networkState.url.empty()) {
			std::cout << "Relative update_package_root_url requires absolute version_manifest_url." << std::endl;
			SetLauncherStatus(L"Failed: unresolved relative update URL.");
			LogUpdateError(
				"UF-BOOTSTRAP-URL",
				"LauncherUpdateCoordinator::FetchBootstrapConfig",
				"Relative update root cannot be resolved",
				"update_package_root_url is relative and version manifest URL is not absolute.");
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
		SetLauncherStatus(L"Failed: cannot resolve download host.");
		LogUpdateError(
			"UF-BOOTSTRAP-URL",
			"LauncherUpdateCoordinator::FetchBootstrapConfig",
			"Bootstrap cannot resolve download host/path",
			"Neither update root URL nor absolute version manifest base resolved.");
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
		SetLauncherStatus(L"Failed: missing base package URL.");
		LogUpdateError(
			"UF-BOOTSTRAP-FIELD",
			"LauncherUpdateCoordinator::FetchBootstrapConfig",
			"Bootstrap missing base package URL list",
			"base_package_urls/base_package_url fields are empty.");
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
	SetLauncherStatus(L"Bootstrap configuration ready.");
	return true;
}

bool LauncherUpdateCoordinator::RefreshRemoteVersionManifest()
{
	SetLauncherStatus(L"Refreshing remote manifest...");
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
		LogUpdateError(
			"UF-MANIFEST-HTTP",
			"LauncherUpdateCoordinator::RefreshRemoteVersionManifest",
			"Version manifest HTTP request failed",
			std::string("path=") + resolvedPath + ", retries=" + std::to_string(kManifestFetchMaxAttempts),
			0,
			res ? res->status : 0,
			errorCode);
		SetLauncherStatus(L"Failed: remote manifest request.");
		return false;
	}
	if (res->body.empty()) {
		std::cout << "Version.dat response body is empty, path: " << resolvedPath << std::endl;
		LogUpdateError(
			"UF-MANIFEST-EMPTY",
			"LauncherUpdateCoordinator::RefreshRemoteVersionManifest",
			"Version manifest response body is empty",
			std::string("path=") + resolvedPath);
		SetLauncherStatus(L"Failed: manifest content is empty.");
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
		LogUpdateError(
			"UF-MANIFEST-PARSE",
			"LauncherUpdateCoordinator::RefreshRemoteVersionManifest",
			"Version manifest format not supported",
			std::string("path=") + resolvedPath + ", body_size=" + std::to_string(res->body.size()));
		SetLauncherStatus(L"Failed: manifest format parse.");
		return false;
	}

	WriteVersionToMapping(manifestJsonText);

	std::string strRemoteVersionDatMd5 = FileHash::string_md5(manifestBinary);
	const auto localFilesBeforeRefresh = m_versionState.files;
	std::map<std::string, VersionConfig> remoteFiles;
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
		remoteFiles[config.m_strPage] = config;
		try {
			const std::filesystem::path localPath =
				std::filesystem::current_path() / std::filesystem::u8path(config.m_strPage);
			std::error_code ec;
			const auto parent = localPath.parent_path();
			if (!parent.empty() && !std::filesystem::exists(parent, ec))
			{
				std::filesystem::create_directories(parent, ec);
			}
		}
		catch (...) {
			std::cout << "Skip invalid local page path: " << config.m_strPage << std::endl;
			remoteFiles.erase(config.m_strPage);
		}
	}

	if (strRemoteVersionDatMd5 != m_versionState.localVersionMD5) {
		std::ofstream ofs("Version.dat", std::ios::binary);
		ofs.write(manifestBinary.data(), manifestBinary.size());
		ofs.close();
	}

	std::error_code versionDatEc;
	const bool localVersionDatExists = std::filesystem::exists(std::filesystem::u8path("Version.dat"), versionDatEc) && !versionDatEc;
	const bool hasLocalManifestBaseline = localVersionDatExists && !localFilesBeforeRefresh.empty();
	size_t manifestDiffCount = 0;
	size_t sizeValidationDiffCount = 0;

	m_versionState.files = std::move(remoteFiles);
	m_versionState.runtimeList = BuildPrelaunchSyncList(
		localFilesBeforeRefresh,
		m_versionState.files,
		hasLocalManifestBaseline,
		manifestDiffCount,
		sizeValidationDiffCount);

	if (hasLocalManifestBaseline) {
		std::cout << "Prelaunch sync list from local/remote Version.dat diff: "
			<< m_versionState.runtimeList.size()
			<< " (diff_count=" << manifestDiffCount << ")" << std::endl;
	}
	else {
		std::cout << "Local Version.dat missing or invalid, prelaunch full size validation diff count: "
			<< m_versionState.runtimeList.size()
			<< " (size_diff_count=" << sizeValidationDiffCount << ")" << std::endl;
	}
	m_versionState.localVersionMD5 = strRemoteVersionDatMd5;

	SetLauncherStatus(L"Remote manifest refreshed.");
	return true;
}
