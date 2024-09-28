#include "framework.h"
#include "WorkThread.h"
#include <filesystem>
#include <httplib.h>
#include <zstd.h>
#include <json/json.h>
#include <wincrypt.h>
#include "FileHash.h"

#pragma comment(lib, "advapi32.lib")

WorkThread::WorkThread()
{
	// �����߳�
	DWORD dwThreadId = 0;
	m_hThread = CreateThread(NULL, 0, ThreadProc, this, 0, &dwThreadId);
	if (m_hThread == NULL) {
		HandleError("CreateThread failed");
	}
}

WorkThread::~WorkThread()
{
}

DWORD __stdcall WorkThread::ThreadProc(LPVOID lpParameter)
{
	WorkThread* pThis = (WorkThread*)lpParameter;
	if (pThis) {
		return pThis->Run();
	}
	return 0;
}

DWORD WorkThread::Run()
{
	// �Ȱѱ��ص�Version.dat�ļ���ȡ����
	{
		std::string strLocalVersionDatContent;
		std::ifstream ifs("Version.dat");
		if (ifs.is_open()) {
			std::stringstream buffer;
			buffer << ifs.rdbuf();
			strLocalVersionDatContent = buffer.str();
			ifs.close();
		}

		if (!strLocalVersionDatContent.empty())
		{
			// ���� Version.dat�ļ�
			std::string strLocalVersionDat = DecryptVersionDat(strLocalVersionDatContent);
			// ����һ�����Ͻṹ��JSON����
			Json::Value root;
			Json::Reader reader;
			if (reader.parse(strLocalVersionDat, root)) {
				// ��ȡ�汾��
				m_qwVersion = root["time"].asInt64();
				// ��ȡ�ļ���Ϣ
				Json::Value filesJson = root["file"];
				for (auto& fileJson : filesJson) {
					VersionConfig config;
					config.m_strMd5 = fileJson["md5"].asString();
					config.m_qwTime = fileJson["time"].asInt64();
					config.m_qwSize = fileJson["size"].asInt64();
					config.m_strPage = fileJson["page"].asString();
					m_mapFiles[config.m_strPage] = config;
				}

				// ��ȡ runtime �ļ��б�
				Json::Value downloadList = root["runtime"];
				for (auto& download : downloadList) {
					m_vecRunTimeList.push_back(download.asString());
				}
			}
		}
	}

	// ���ظ����ļ��ĵ�ַ https://gitee.com/MengMianHeiYiRen/MagicShow/blob/master/ReadMe.txt
	httplib::Client cli{ "gitee.com",433 };
	auto res = cli.Get("/MengMianHeiYiRen/MagicShow/raw/master/ReadMe.txt");
	if (res && res->status == 200) {
		std::string ciphertext;
		for (size_t i = 0; i < res->body.size(); i += 2) {
			char hex[3] = { res->body[i], res->body[i + 1], 0 };
			ciphertext += (char)strtol(hex, NULL, 16);
		}
		std::string strVersionDatUrl = DecryptUrl(ciphertext);

		// ���ظ����ļ� �µ�ַ strVersionDatUrl HOSTҲ�������� /Version.dat
		// ���� strVersionDatUrl ����û�ж˿�,û�о�Ĭ��80
		size_t pos = strVersionDatUrl.find(':');
		std::string strHost = strVersionDatUrl.substr(0, pos);
		short wPort = 80;
		if (pos != std::string::npos) {
			wPort = atoi(strVersionDatUrl.substr(pos + 1).c_str());
			strVersionDatUrl = strVersionDatUrl.substr(0, pos);
		}
		m_strHost = strVersionDatUrl;
		m_wPort = wPort;

		httplib::Client cli2(strVersionDatUrl,wPort);
		auto res2 = cli2.Get("/Version.dat");
		if (res2 && res2->status == 200) {
			std::string strVersionDatContent = DecryptVersionDat(res2->body);
			// ����һ�����Ͻṹ��JSON����
			Json::Value root;
			Json::Reader reader;
			if (reader.parse(strVersionDatContent, root)) {
				// ��ȡ�汾��
				if (m_qwVersion != root["time"].asInt64())
				{
					m_qwVersion = root["time"].asInt64();
					m_mapFiles.clear();
					// ��ȡ�ļ���Ϣ
					Json::Value filesJson = root["file"];
					for (auto& fileJson : filesJson) {
						VersionConfig config;
						config.m_strMd5 = fileJson["md5"].asString();
						config.m_qwTime = fileJson["time"].asInt64();
						config.m_qwSize = fileJson["size"].asInt64();
						config.m_strPage = fileJson["page"].asString();
						m_mapFiles[config.m_strPage] = config;
					}

					m_vecRunTimeList.clear();

					// ��ȡ runtime �ļ��б�
					Json::Value downloadList = root["runtime"];
					for (auto& download : downloadList) {
						m_vecRunTimeList.push_back(download.asString());
					}
				}
			}
		}

		// ����RunTime�ļ�
		DownloadRunTimeFile(m_strHost,wPort);
	} 

	// ������Ϸ
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	if (!CreateProcess(L"MapleReborn.exe", NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		HandleError("CreateProcess failed");
	}

	m_hGameProcess = pi.hProcess;
	if (m_hGameProcess)
	{
		WaitForSingleObject(m_hGameProcess, INFINITE);
	}

	if (pi.hProcess)
	{
		CloseHandle(pi.hProcess);
	}

	if (pi.hThread)
	{
		CloseHandle(pi.hThread);
	}

	return 0;
}

void WorkThread::HandleError(const char* msg) {
	std::cerr << msg << " Error: " << GetLastError() << std::endl;
	exit(1);
}

std::string WorkThread::DecryptUrl(const std::string& ciphertext)
{
	HCRYPTPROV hProv;
	HCRYPTKEY hKey;
	HCRYPTHASH hHash;

	// ��ȡ�����ṩ������
	if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		HandleError("CryptAcquireContext failed");
		return ciphertext;
	}

	// ������ϣ����
	if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
		HandleError("CryptCreateHash failed");
		return ciphertext;
	}

	// ��ϣ��Կ
	const char* key = "cDds!ErF9sIe6u$B";
	if (!CryptHashData(hHash, (BYTE*)key, strlen(key), 0)) {
		HandleError("CryptHashData failed");
		return ciphertext;
	}

	// ���� AES ��Կ
	if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
		HandleError("CryptDeriveKey failed");
		return ciphertext;
	}

	DWORD dataLen = ciphertext.length();
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
	// ��Ӧ���ϼ��ܹ��������� ��
	std::string strDict = "KRu998Am";
	ZSTD_DDict* ddict = ZSTD_createDDict(strDict.c_str(), strDict.length());
	std::string strCompress = ciphertext;
	std::string strJson;
	ZSTD_DCtx* dctx = ZSTD_createDCtx();
	size_t decompressBound = ZSTD_getFrameContentSize(strCompress.c_str(), strCompress.length());
	strJson.resize(decompressBound);
	size_t decompressSize = ZSTD_decompress_usingDDict(dctx, &strJson[0], decompressBound, strCompress.c_str(), strCompress.length(), ddict);
	strJson.resize(decompressSize);
	ZSTD_freeDDict(ddict);
	ZSTD_freeDCtx(dctx);

	return ciphertext;
}

void WorkThread::DownloadRunTimeFile(const std::string& strHost, const short wPort)
{
	m_nTotalDownload = m_vecRunTimeList.size();
	m_nCurrentDownload = 0;
	for (auto& download : m_vecRunTimeList)
	{
		// �Ȳ鿴�����Ƿ��������ļ�
		std::string strLocalFile = download;
		std::string strLocalFileMd5 = FileHash::file_md5(strLocalFile);

		// �Ա�MD5
		if (m_mapFiles.find(strLocalFile) != m_mapFiles.end())
		{
			if (m_mapFiles[strLocalFile].m_strMd5 == strLocalFileMd5)
			{
				m_nCurrentDownload += 1;
				continue;
			}
		}

		// ɾ�������ļ�
		std::filesystem::remove(strLocalFile);
		// �������ļ�
		httplib::Client cli(strHost, wPort);
		auto res = cli.Get(download.c_str());
		if (res && res->status == 200) {
			std::ofstream ofs(strLocalFile, std::ios::binary);
			ofs.write(res->body.c_str(), res->body.size());
			ofs.close();
		}
		m_nCurrentDownload += 1;
	}
}

int WorkThread::GetTotalDownload() const
{
	return m_nTotalDownload;
}

int WorkThread::GetCurrentDownload() const
{
	return m_nCurrentDownload;
}