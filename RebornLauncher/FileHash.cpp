#include "FileHash.h"

#include <windows.h>
#include <wincrypt.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {
std::string BytesToHex(const BYTE* data, DWORD length) {
	std::ostringstream oss;
	oss << std::hex << std::uppercase << std::setfill('0');
	for (DWORD i = 0; i < length; ++i) {
		oss << std::setw(2) << static_cast<int>(data[i]);
	}
	return oss.str();
}

std::string ComputeMd5FromStream(std::istream& stream) {
	HCRYPTPROV hProv = 0;
	HCRYPTHASH hHash = 0;
	if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
		return {};
	}
	if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
		CryptReleaseContext(hProv, 0);
		return {};
	}

	char buffer[4096];
	while (stream.good()) {
		stream.read(buffer, sizeof(buffer));
		const auto readCount = stream.gcount();
		if (readCount > 0) {
			if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(buffer), static_cast<DWORD>(readCount), 0)) {
				CryptDestroyHash(hHash);
				CryptReleaseContext(hProv, 0);
				return {};
			}
		}
	}

	DWORD hashLen = 0;
	DWORD lenSize = sizeof(hashLen);
	if (!CryptGetHashParam(hHash, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hashLen), &lenSize, 0)) {
		CryptDestroyHash(hHash);
		CryptReleaseContext(hProv, 0);
		return {};
	}
	std::vector<BYTE> hash(hashLen);
	if (!CryptGetHashParam(hHash, HP_HASHVAL, hash.data(), &hashLen, 0)) {
		CryptDestroyHash(hHash);
		CryptReleaseContext(hProv, 0);
		return {};
	}

	CryptDestroyHash(hHash);
	CryptReleaseContext(hProv, 0);
	return BytesToHex(hash.data(), hashLen);
}
}  // namespace

std::string FileHash::string_md5(const std::string& str)
{
	std::istringstream iss(str);
	return ComputeMd5FromStream(iss);
}

std::string FileHash::file_md5(const std::string& filename)
{
	std::ifstream ifs(filename, std::ios::binary);
	if (!ifs.is_open()) {
		return {};
	}
	return ComputeMd5FromStream(ifs);
}
