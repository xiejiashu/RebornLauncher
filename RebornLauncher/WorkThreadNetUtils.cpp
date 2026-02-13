#include "framework.h"
#include "WorkThreadNetUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>

namespace workthread::netutils {

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

uint64_t ParseTotalSizeFromResponse(const httplib::Response& response) {
	const std::string contentRange = response.get_header_value("Content-Range");
	if (!contentRange.empty()) {
		const size_t slash = contentRange.rfind('/');
		if (slash != std::string::npos && slash + 1 < contentRange.size()) {
			try {
				return static_cast<uint64_t>(std::stoull(contentRange.substr(slash + 1)));
			}
			catch (...) {
			}
		}
	}

	const std::string contentLength = response.get_header_value("Content-Length");
	if (!contentLength.empty()) {
		try {
			return static_cast<uint64_t>(std::stoull(contentLength));
		}
		catch (...) {
		}
	}
	return 0;
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

} // namespace workthread::netutils
