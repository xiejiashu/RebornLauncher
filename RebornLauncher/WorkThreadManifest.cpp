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

namespace {

constexpr const char* kBootstrapHost = "https://gitee.com";
constexpr const char* kBootstrapPath = "/MengMianHeiYiRen/MagicShow/raw/master/RemoteEncrypt.txt";

std::string NormalizeRelativeUrlPath(std::string path) {
	std::replace(path.begin(), path.end(), '\\', '/');
	if (path.empty()) {
		return {};
	}
	if (path.front() != '/') {
		path.insert(path.begin(), '/');
	}
	return path;
}

std::string JoinUrlPath(const std::string& basePath, const std::string& childPath) {
	std::string base = basePath;
	std::string child = childPath;
	std::replace(base.begin(), base.end(), '\\', '/');
	std::replace(child.begin(), child.end(), '\\', '/');

	if (base.empty()) {
		return NormalizeRelativeUrlPath(child);
	}
	if (base.front() != '/') {
		base.insert(base.begin(), '/');
	}
	if (!base.empty() && base.back() != '/') {
		base.push_back('/');
	}
	while (!child.empty() && child.front() == '/') {
		child.erase(child.begin());
	}
	return base + child;
}

bool IsHttpUrl(const std::string& value) {
	return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

bool ParseHttpUrl(const std::string& url, bool& useTls, std::string& host, int& port, std::string& path) {
	std::regex urlRegex(R"((https?)://([^/:]+)(?::(\d+))?(\/.*)?)");
	std::smatch match;
	if (!std::regex_match(url, match, urlRegex)) {
		return false;
	}
	useTls = match[1].str() == "https";
	host = match[2].str();
	port = match[3].matched ? std::stoi(match[3].str()) : (useTls ? 443 : 80);
	path = match[4].matched ? match[4].str() : "/";
	if (path.empty()) {
		path = "/";
	}
	if (path.front() != '/') {
		path.insert(path.begin(), '/');
	}
	return true;
}

bool ExtractBaseAndPath(const std::string& absoluteUrl, std::string& baseUrl, std::string& path) {
	bool useTls = false;
	std::string host;
	int port = 0;
	if (!ParseHttpUrl(absoluteUrl, useTls, host, port, path)) {
		return false;
	}
	const bool defaultPort = (useTls && port == 443) || (!useTls && port == 80);
	baseUrl = (useTls ? "https://" : "http://") + host;
	if (!defaultPort) {
		baseUrl += ":" + std::to_string(port);
	}
	return true;
}

std::string TrimAscii(std::string value) {
	const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
		value.erase(value.begin());
	}
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
		value.pop_back();
	}
	return value;
}

std::string DirnamePath(std::string path) {
	if (path.empty()) {
		return "/";
	}
	std::replace(path.begin(), path.end(), '\\', '/');
	const size_t queryPos = path.find('?');
	if (queryPos != std::string::npos) {
		path = path.substr(0, queryPos);
	}
	size_t slash = path.find_last_of('/');
	if (slash == std::string::npos) {
		return "/";
	}
	path = path.substr(0, slash + 1);
	if (path.empty()) {
		return "/";
	}
	if (path.front() != '/') {
		path.insert(path.begin(), '/');
	}
	return path;
}

std::string GetFileNameFromUrl(std::string url) {
	const size_t hashPos = url.find('#');
	if (hashPos != std::string::npos) {
		url = url.substr(0, hashPos);
	}
	const size_t queryPos = url.find('?');
	if (queryPos != std::string::npos) {
		url = url.substr(0, queryPos);
	}
	std::replace(url.begin(), url.end(), '\\', '/');
	const size_t slash = url.find_last_of('/');
	if (slash == std::string::npos) {
		return url;
	}
	if (slash + 1 >= url.size()) {
		return {};
	}
	return url.substr(slash + 1);
}

bool HexBodyToBytes(const std::string& body, std::string& out) {
	std::string hex;
	hex.reserve(body.size());
	for (unsigned char ch : body) {
		if (std::isxdigit(ch) != 0) {
			hex.push_back(static_cast<char>(ch));
		}
	}
	if (hex.empty() || (hex.size() % 2) != 0) {
		return false;
	}

	std::string decoded;
	decoded.reserve(hex.size() / 2);
	for (size_t i = 0; i < hex.size(); i += 2) {
		char pair[3] = { hex[i], hex[i + 1], 0 };
		char* endPtr = nullptr;
		const long value = strtol(pair, &endPtr, 16);
		if (endPtr == nullptr || *endPtr != '\0') {
			return false;
		}
		decoded.push_back(static_cast<char>(value & 0xFF));
	}
	out = std::move(decoded);
	return true;
}

void MergeUnique(std::vector<std::string>& target, const std::vector<std::string>& source) {
	for (const auto& item : source) {
		if (item.empty()) {
			continue;
		}
		if (std::find(target.begin(), target.end(), item) == target.end()) {
			target.push_back(item);
		}
	}
}

std::vector<std::string> ReadStringArray(const Json::Value& parent, const char* key) {
	std::vector<std::string> values;
	if (!parent.isMember(key) || !parent[key].isArray()) {
		return values;
	}
	for (const auto& v : parent[key]) {
		if (v.isString()) {
			std::string s = TrimAscii(v.asString());
			if (!s.empty()) {
				values.push_back(s);
			}
		}
	}
	return values;
}

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

	m_basePackageUrls = ReadStringArray(content, "base_package_urls");
	if (m_basePackageUrls.empty()) {
		const std::string singleBasePackageUrl = TrimAscii(content["base_package_url"].asString());
		if (!singleBasePackageUrl.empty()) {
			m_basePackageUrls.push_back(singleBasePackageUrl);
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
		m_strVersionManifestPath = versionManifestUrl;
	}

	if (!updateRootUrl.empty() && IsHttpUrl(updateRootUrl)) {
		if (!ExtractBaseAndPath(updateRootUrl, m_strUrl, m_strPage)) {
			std::cout << "Invalid update_package_root_url: " << updateRootUrl << std::endl;
			return false;
		}
	}
	else if (!updateRootUrl.empty()) {
		if (!versionBaseUrl.empty()) {
			m_strUrl = versionBaseUrl;
		}
		else if (m_strUrl.empty()) {
			std::cout << "Relative update_package_root_url requires absolute version_manifest_url." << std::endl;
			return false;
		}
		m_strPage = NormalizeRelativeUrlPath(updateRootUrl);
		if (m_strPage.back() != '/') {
			m_strPage.push_back('/');
		}
	}
	else if (!versionBaseUrl.empty()) {
		m_strUrl = versionBaseUrl;
		m_strPage = DirnamePath(versionPath);
	}
	else {
		std::cout << "Bootstrap JSON cannot resolve download host/path." << std::endl;
		return false;
	}

	if (m_strPage.empty()) {
		m_strPage = "/";
	}
	if (m_strPage.front() != '/') {
		m_strPage.insert(m_strPage.begin(), '/');
	}
	if (m_strPage.back() != '/') {
		m_strPage.push_back('/');
	}
	if (!versionManifestIsAbsolute) {
		const bool rootedPath = !versionManifestUrl.empty() &&
			(versionManifestUrl.front() == '/' || versionManifestUrl.front() == '\\');
		if (rootedPath) {
			m_strVersionManifestPath = NormalizeRelativeUrlPath(versionManifestUrl);
		}
		else {
			m_strVersionManifestPath = JoinUrlPath(m_strPage, versionManifestUrl);
		}
	}
	if (m_strVersionManifestPath.empty()) {
		m_strVersionManifestPath = JoinUrlPath(m_strPage, "Version.dat");
	}

	if (m_basePackageUrls.empty()) {
		std::cout << "Bootstrap JSON missing base_package_urls/base_package_url." << std::endl;
		return false;
	}

	Json::Value p2pJson = root["p2p"];
	if (p2pJson.isObject()) {
		std::lock_guard<std::mutex> lock(m_p2pMutex);

		const std::string signalUrl = TrimAscii(p2pJson["signal_url"].asString());
		if (!signalUrl.empty() && m_p2pSettings.signalEndpoint.empty()) {
			m_p2pSettings.signalEndpoint = signalUrl;
		}

		const std::string signalToken = TrimAscii(p2pJson["signal_auth_token"].asString());
		if (!signalToken.empty() && m_p2pSettings.signalAuthToken.empty()) {
			m_p2pSettings.signalAuthToken = signalToken;
		}

		auto remoteStuns = ReadStringArray(p2pJson, "stun_servers");
		if (!remoteStuns.empty()) {
			MergeUnique(remoteStuns, m_p2pSettings.stunServers);
			m_p2pSettings.stunServers = std::move(remoteStuns);
		}
	}

	std::cout << "Bootstrap resolved: host=" << m_strUrl
		<< " updateRoot=" << m_strPage
		<< " versionPath=" << m_strVersionManifestPath << std::endl;
	return true;
}

bool WorkThread::RefreshRemoteVersionManifest()
{
	const std::string strVersionDatPath = m_strVersionManifestPath.empty()
		? JoinUrlPath(m_strPage, "Version.dat")
		: m_strVersionManifestPath;

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

		if (!m_client) {
			return httplib::Result();
		}
		return m_client->Get(requestTarget.c_str(), headers);
	};

	httplib::Result res = fetchManifest(strVersionDatPath);
	std::string resolvedPath = strVersionDatPath;

	if ((!res || res->status != 200 || res->body.empty()) && !IsHttpUrl(strVersionDatPath)) {
		const std::string fileName = GetFileNameFromUrl(strVersionDatPath);
		const std::string fallbackPath = JoinUrlPath(m_strPage, fileName.empty() ? "Version.dat" : fileName);
		if (fallbackPath != strVersionDatPath) {
			httplib::Result fallbackRes = fetchManifest(fallbackPath);
			if (fallbackRes && fallbackRes->status == 200 && !fallbackRes->body.empty()) {
				res = std::move(fallbackRes);
				resolvedPath = fallbackPath;
				m_strVersionManifestPath = fallbackPath;
			}
		}
	}

	if (!res || res->status != 200) {
		const std::string statusText = res ? std::to_string(res->status) : std::string("none");
		const int errorCode = static_cast<int>(res.error());
		std::cout << "Failed to fetch Version.dat from " << resolvedPath
			<< ", status: " << statusText
			<< ", error: " << errorCode
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
	if (strRemoteVersionDatMd5 != m_strLocalVersionMD5)
	{
		std::ofstream ofs("Version.dat", std::ios::binary);
		ofs.write(manifestBinary.data(), manifestBinary.size());
		ofs.close();
		m_qwVersion = root["time"].asInt64();
		m_mapFiles.clear();
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
			m_mapFiles[config.m_strPage] = config;
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
				m_mapFiles.erase(config.m_strPage);
			}
		}
		m_vecRunTimeList.clear();
		Json::Value downloadList = root["runtime"];
		for (auto& download : downloadList) {
			m_vecRunTimeList.push_back(download.asString());
		}
	}

	return true;
}
