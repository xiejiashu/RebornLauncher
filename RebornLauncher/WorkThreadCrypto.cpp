#include "framework.h"
#include "WorkThread.h"

#include <cstring>
#include <iostream>

#include <wincrypt.h>
#include <zstd.h>

#pragma comment(lib, "advapi32.lib")

void WorkThread::HandleError(const char* msg) {
	std::cerr << msg << " Error: " << GetLastError() << std::endl;
	exit(1);
}

std::string WorkThread::DecryptConfigPayload(const std::string& ciphertext)
{
	HCRYPTPROV hProv;
	HCRYPTKEY hKey;
	HCRYPTHASH hHash;

	if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		HandleError("CryptAcquireContext failed");
		return ciphertext;
	}

	if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
		HandleError("CryptCreateHash failed");
		return ciphertext;
	}

	const char* key = "cDds!ErF9sIe6u$B";
	if (!CryptHashData(hHash, (BYTE*)key, strlen(key), 0)) {
		HandleError("CryptHashData failed");
		return ciphertext;
	}

	if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
		HandleError("CryptDeriveKey failed");
		return ciphertext;
	}

	DWORD dataLen = static_cast<DWORD>(ciphertext.length());
	DWORD bufferLen = dataLen;

	if (bufferLen > 0)
	{
		std::string buffer(bufferLen + 1, 0);
		memcpy(&buffer[0], ciphertext.c_str(), dataLen);

		if (!CryptDecrypt(hKey, 0, TRUE, 0, (BYTE*)buffer.data(), &dataLen)) {
			HandleError("CryptDecrypt failed");
		}
		buffer.resize(dataLen);

		return buffer;
	}

	return ciphertext;
}

std::string WorkThread::DecryptVersionDat(const std::string& ciphertext)
{
	if (ciphertext.empty()) {
		return {};
	}

	const size_t decompressBound = ZSTD_getFrameContentSize(ciphertext.data(), ciphertext.size());
	if (decompressBound == ZSTD_CONTENTSIZE_ERROR || decompressBound == ZSTD_CONTENTSIZE_UNKNOWN || decompressBound == 0 ||
		decompressBound > (64ULL * 1024ULL * 1024ULL)) {
		std::cout << "Invalid Version.dat payload size: " << decompressBound << std::endl;
		return {};
	}

	const std::string strDict = "D2Qbzy7hnmLh1zqgmDKx";
	ZSTD_DDict* ddict = ZSTD_createDDict(strDict.data(), strDict.size());
	if (!ddict) {
		return {};
	}

	ZSTD_DCtx* dctx = ZSTD_createDCtx();
	if (!dctx) {
		ZSTD_freeDDict(ddict);
		return {};
	}

	std::string strJson;
	strJson.resize(decompressBound);
	const size_t decompressSize = ZSTD_decompress_usingDDict(
		dctx, &strJson[0], decompressBound, ciphertext.data(), ciphertext.size(), ddict);

	ZSTD_freeDDict(ddict);
	ZSTD_freeDCtx(dctx);

	if (ZSTD_isError(decompressSize) != 0) {
		std::cout << "Failed to decompress Version.dat: " << ZSTD_getErrorName(decompressSize) << std::endl;
		return {};
	}

	strJson.resize(decompressSize);
	return strJson;
}
