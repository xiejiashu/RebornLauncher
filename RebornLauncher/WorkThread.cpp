#include "framework.h"
#include "WorkThread.h"
#include <filesystem>
#include <httplib.h>
#include <zstd.h>
#include <json/json.h>
#include <wincrypt.h>
#include "FileHash.h"
#include <TlHelp32.h>
#include <Psapi.h>

#pragma comment(lib, "advapi32.lib")

WorkThread::WorkThread()
{
	// �����߳�
	DWORD dwThreadId = 0;
	m_hThread = CreateThread(NULL, 0, ThreadProc, this, 0, &dwThreadId);
	if (m_hThread == NULL) {
		HandleError("CreateThread failed");
	}

	// �� m_hFileMappings �ͷŵ�
	for (auto hFileMapping : m_hFileMappings)
	{
		CloseHandle(hFileMapping);
	}
}

WorkThread::~WorkThread()
{
}

DWORD __stdcall WorkThread::ThreadProc(LPVOID lpParameter)
{
	// �����������ֽ� MapleReborn.exe �Ľ���
	// 1. ��ȡ���н���
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32 pe;
		pe.dwSize = sizeof(PROCESSENTRY32);
		if (Process32First(hSnapshot, &pe)) {
			do {
				HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
				if (hProcess) {
					TCHAR processPath[MAX_PATH];
					if (GetModuleFileNameEx(hProcess, NULL, processPath, MAX_PATH)) {
						std::wstring strProcessPath = processPath;
						if (strProcessPath.find(L"MapleReborn.exe") != std::string::npos){
							TerminateProcess(hProcess, 0);
						}
						if (strProcessPath.find(L"MapleStory.exe") != std::string::npos) {
							TerminateProcess(hProcess, 0);
						}
					}
					CloseHandle(hProcess);
				}
			} while (Process32Next(hSnapshot, &pe));
		}
		CloseHandle(hSnapshot);
	}

	WorkThread* pThis = (WorkThread*)lpParameter;
	if (pThis) {
		return pThis->Run();
	}
	return 0;
}

DWORD WorkThread::Run()
{
	// �Ȱѱ��ص�Version.dat�ļ���ȡ����
	std::string strLocalVersionDatMd5;
	{
		std::cout << "9999999999999999999" << std::endl;
		std::string strLocalVersionDatContent;
		std::ifstream ifs("Version.dat", std::ios::binary);
		if (ifs.is_open()) {
			std::stringstream buffer;
			buffer << ifs.rdbuf();
			strLocalVersionDatContent = buffer.str();
			ifs.close();
		}
		std::cout << "aaaaaaaaaaaaaaaaaaaaaaa" << std::endl;
		strLocalVersionDatMd5 = FileHash::string_md5(strLocalVersionDatContent);

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
	std::cout << "bbbbbbbbbbbbbbbbbbbbbbbbbbb" << std::endl;
	// ���ظ����ļ��ĵ�ַ 
	httplib::Client cli{ "https://gitee.com" };
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
			const std::string strVersionDatContent = DecryptVersionDat(res2->body);
			// ����һ�����Ͻṹ��JSON����
			Json::Value root;
			Json::Reader reader;
			if (reader.parse(strVersionDatContent, root)) {
				// ��ȡԶ�̵�MD5
				std::string strRemoteVersionDatMd5 = FileHash::string_md5(res2->body);
				// ��ȡ�汾��
				if (strRemoteVersionDatMd5 != strLocalVersionDatMd5)
				{
					// ���浽����
					std::ofstream ofs("Version.dat", std::ios::binary);
					ofs.write(res2->body.data(), res2->body.size());
					ofs.close();

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
		std::cout << "cccccccccccccccccccccccccc" << std::endl;
		// ����RunTime�ļ�
		DownloadRunTimeFile(m_strHost,wPort);
	} 

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	unsigned long long dwTick = GetTickCount64();


	// ��MD5д��MAP��
	WriteDataToMapping();

	unsigned long long dwNewTick = GetTickCount64();
	std::cout << "WriteDataToMapping ����ʱ��:" << dwNewTick - dwTick << std::endl;
	dwTick = dwNewTick;

	// ������Ϸ
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	if (!CreateProcess(L"MapleReborn.exe", NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		HandleError("CreateProcess failed");
	}

	// Create���̻���ʱ��
	dwNewTick = GetTickCount64();
	std::cout << "CreateProcess ����ʱ��:" << dwNewTick - dwTick << std::endl;

	m_hGameProcess = pi.hProcess;

	// ������������
	httplib::Server svr;
	svr.Get("/download", [&](const httplib::Request& req, httplib::Response& res) {
		std::string strPage = req.get_param_value("page");
		std::cout << "ddddddddddddd download page:"<< strPage << std::endl;
		auto it = m_mapFiles.find(strPage);
		if (it != m_mapFiles.end())
		{
			std::string strLocalFile = it->first;
			bool Md5Same = false;
			if (GetFileAttributesA(strLocalFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
				std::string strLocalFileMd5 = FileHash::file_md5(strLocalFile);
				Md5Same = it->second.m_strMd5 == strLocalFileMd5;
			}

			if (Md5Same)
			{
				res.status = 200;
				res.set_content("OK", "text/plain");
			}
			else
			{
				// ����
				httplib::Client cli(m_strHost, m_wPort);
				strPage = "/Update/" + std::to_string(it->second.m_qwTime) + "/" + strPage;
				std::replace(strPage.begin(), strPage.end(), '\\', '/');
				auto ret = cli.Get(strPage);
				if (ret && ret->status == 200)
				{
					// std::filesystem::create_directories(strLocalFile);
					// ��ȡĿ¼
					std::filesystem::path filePath = strLocalFile;
					std::filesystem::create_directories(filePath.parent_path());
					std::ofstream ofs(strLocalFile, std::ios::binary);
					ofs.write(ret->body.c_str(), ret->body.size());
					ofs.close();
					res.status = 200;
					res.set_content("OK", "text/plain");
					std::cout << __FILE__<<":" << __LINE__ << std::endl;
				}
				else
				{
					res.status = 404;
					res.set_content("Not Found", "text/plain");
					std::cout << __FILE__ << ":" << __LINE__ << std::endl;
				}
			}

			// ����һ��OK���� ����Ҫjson
			res.status = 200;
			res.set_content("OK", "text/plain");
			std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		}
		else {
			std::cout << "û������ļ�" << strPage <<std::endl;
			res.status = 404;
			res.set_content("404", "text/palin");
		}
	});

	// svr.bind_to_port("localhost", 12345);
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	svr.listen("localhost", 12345);

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

	return strJson;
}

void WorkThread::DownloadRunTimeFile(const std::string& strHost, const short wPort)
{
	m_nTotalDownload = m_vecRunTimeList.size();
	m_nCurrentDownload = 0;
	for (auto& download : m_vecRunTimeList)
	{
		// �Ȳ鿴�����Ƿ��������ļ�
		std::string strLocalFile = download;
		std::string strPage = "/Update/" + std::to_string(m_mapFiles[download].m_qwTime) + "/" + download;
		std::replace(strPage.begin(), strPage.end(), '\\', '/');

		std::cout <<__FUNCTION__<<":" << strPage << std::endl;

		// �Ա�MD5
		auto it = m_mapFiles.find(strLocalFile);
		if (it != m_mapFiles.end())
		{
			bool Md5Same = false;
			if (GetFileAttributesA(strLocalFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
				std::string strLocalFileMd5 = FileHash::file_md5(strLocalFile);
				Md5Same = it->second.m_strMd5 == strLocalFileMd5;
				std::cout << "md51:" << it->second.m_strMd5 << "vs md52:"<< strLocalFileMd5 << std::endl;
			}

			if (Md5Same)
			{
				m_nCurrentDownload += 1;
				continue;
			}
		}
		
		std::cout << __FILE__ << ":" << __LINE__<< " �ļ�:" << strLocalFile << std::endl;
		// ɾ�������ļ�
		// std::filesystem::remove(strLocalFile);
		DeleteFileA(strLocalFile.c_str());

		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		// �������ļ�
		httplib::Client cli(strHost, wPort);
		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		auto res = cli.Get(strPage);
		std::cout << __FILE__ << ":" << __LINE__ << strPage << std::endl;
		if (res && res->status == 200) {
			std::ofstream ofs(strLocalFile, std::ios::binary);
			ofs.write(res->body.c_str(), res->body.size());
			ofs.close();
		}
		m_nCurrentDownload += 1;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
}

int WorkThread::GetTotalDownload() const
{
	return m_nTotalDownload;
}

int WorkThread::GetCurrentDownload() const
{
	return m_nCurrentDownload;
}

void WorkThread::WriteDataToMapping()
{
	// m_mapFiles
	for (auto& [strPage, config] : m_mapFiles)
	{
		std::string strMemoryName = strPage;
		// ɾ�� "\"
		std::replace(strMemoryName.begin(), strMemoryName.end(), '\\', '_');
		HANDLE hFileMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, config.m_strMd5.length() + 1, strMemoryName.c_str());
		if (hFileMapping)
		{
			// ��MD5д��ȥ
			LPVOID lpBaseAddress = MapViewOfFile(hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
			if (lpBaseAddress)
			{
				memcpy(lpBaseAddress, config.m_strMd5.c_str(), config.m_strMd5.length());
				// �����ֹ��
				((char*)lpBaseAddress)[config.m_strMd5.length()] = '\0';
				UnmapViewOfFile(lpBaseAddress);
			}
			m_hFileMappings.push_back(hFileMapping);
		}
	}
}