#include "framework.h"
#include "WorkThreadHttpSession.h"

#include <utility>

#include "WorkThreadNetUtils.h"

namespace {

void ConfigureClient(httplib::Client& client, int readTimeoutSec) {
	client.set_follow_location(true);
	client.set_connection_timeout(8, 0);
	client.set_read_timeout(readTimeoutSec, 0);
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
void ConfigureClient(httplib::SSLClient& client, int readTimeoutSec) {
	client.set_follow_location(true);
	client.set_connection_timeout(8, 0);
	client.set_read_timeout(readTimeoutSec, 0);
}
#endif

} // namespace

namespace workthread::http {

bool DownloadHttpSession::CreateFromAbsoluteUrl(const std::string& absoluteUrl, DownloadHttpSession& outSession) {
	bool useTls = false;
	std::string host;
	int port = 0;
	std::string path;
	if (!workthread::netutils::ParseHttpUrl(absoluteUrl, useTls, host, port, path)) {
		return false;
	}
	outSession.SetEndpoint(useTls, std::move(host), port, std::move(path));
	return true;
}

bool DownloadHttpSession::ProbeRemoteTotalSize(uint64_t& sizeOut, int readTimeoutSec) const {
	sizeOut = 0;
	httplib::Headers headers;
	headers.insert({ "Range", "bytes=0-0" });
	auto res = Get(headers, readTimeoutSec);
	if (!res || (res->status != 200 && res->status != 206)) {
		return false;
	}
	const auto total = workthread::netutils::ParseTotalSizeFromResponse(*res);
	if (total == 0) {
		return false;
	}
	sizeOut = total;
	return true;
}

httplib::Result DownloadHttpSession::Get(const httplib::Headers& headers, int readTimeoutSec) const {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
	if (m_useTls) {
		httplib::SSLClient client(m_host, m_port);
		ConfigureClient(client, readTimeoutSec);
		return client.Get(m_path.c_str(), headers);
	}
#endif
	httplib::Client client(m_host, m_port);
	ConfigureClient(client, readTimeoutSec);
	return client.Get(m_path.c_str(), headers);
}

httplib::Result DownloadHttpSession::Get(const httplib::Headers& headers, const httplib::ContentReceiver& receiver, int readTimeoutSec) const {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
	if (m_useTls) {
		httplib::SSLClient client(m_host, m_port);
		ConfigureClient(client, readTimeoutSec);
		return client.Get(m_path.c_str(), headers, receiver);
	}
#endif
	httplib::Client client(m_host, m_port);
	ConfigureClient(client, readTimeoutSec);
	return client.Get(m_path.c_str(), headers, receiver);
}

httplib::Result DownloadHttpSession::Get(const httplib::ContentReceiver& receiver, int readTimeoutSec) const {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
	if (m_useTls) {
		httplib::SSLClient client(m_host, m_port);
		ConfigureClient(client, readTimeoutSec);
		return client.Get(m_path.c_str(), receiver);
	}
#endif
	httplib::Client client(m_host, m_port);
	ConfigureClient(client, readTimeoutSec);
	return client.Get(m_path.c_str(), receiver);
}

void DownloadHttpSession::SetEndpoint(bool useTls, std::string host, int port, std::string path) {
	m_useTls = useTls;
	m_host = std::move(host);
	m_port = port;
	m_path = std::move(path);
}

} // namespace workthread::http
