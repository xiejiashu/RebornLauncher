#pragma once

#include <cstdint>
#include <string>

#include <httplib.h>

namespace workthread::http {

class DownloadHttpSession {
public:
	static bool CreateFromAbsoluteUrl(const std::string& absoluteUrl, DownloadHttpSession& outSession);

	bool ProbeRemoteTotalSize(uint64_t& sizeOut, int readTimeoutSec = 30) const;
	httplib::Result Get(const httplib::Headers& headers, int readTimeoutSec) const;
	httplib::Result Get(const httplib::Headers& headers, const httplib::ContentReceiver& receiver, int readTimeoutSec) const;
	httplib::Result Get(const httplib::ContentReceiver& receiver, int readTimeoutSec) const;

private:
	void SetEndpoint(bool useTls, std::string host, int port, std::string path);

	bool m_useTls{ false };
	std::string m_host;
	int m_port{ 0 };
	std::string m_path;
};

} // namespace workthread::http
