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
	// 启动线程
	DWORD dwThreadId = 0;
	m_hThread = CreateThread(NULL, 0, ThreadProc, this, 0, &dwThreadId);
	if (m_hThread == NULL) {
		HandleError("CreateThread failed");
	}

	// 把 m_hFileMappings 释放掉
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
	// 结束所有名字叫 MapleReborn.exe 的进程
	// 1. 获取所有进程
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
	// 先把本地的Version.dat文件读取出来
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
			// 解密 Version.dat文件
			std::string strLocalVersionDat = DecryptVersionDat(strLocalVersionDatContent);
			// 这是一个以上结构的JSON内容
			Json::Value root;
			Json::Reader reader;
			if (reader.parse(strLocalVersionDat, root)) {
				// 获取版本号
				m_qwVersion = root["time"].asInt64();
				// 获取文件信息
				Json::Value filesJson = root["file"];
				for (auto& fileJson : filesJson) {
					VersionConfig config;
					config.m_strMd5 = fileJson["md5"].asString();
					config.m_qwTime = fileJson["time"].asInt64();
					config.m_qwSize = fileJson["size"].asInt64();
					config.m_strPage = fileJson["page"].asString();
					m_mapFiles[config.m_strPage] = config;
				}

				// 获取 runtime 文件列表
				Json::Value downloadList = root["runtime"];
				for (auto& download : downloadList) {
					m_vecRunTimeList.push_back(download.asString());
				}
			}
		}
	}
	std::cout << "bbbbbbbbbbbbbbbbbbbbbbbbbbb" << std::endl;
	// 下载更新文件的地址 
	httplib::Client cli{ "https://gitee.com" };
	auto res = cli.Get("/MengMianHeiYiRen/MagicShow/raw/master/ReadMe.txt");
	if (res && res->status == 200) {
		std::string ciphertext;
		for (size_t i = 0; i < res->body.size(); i += 2) {
			char hex[3] = { res->body[i], res->body[i + 1], 0 };
			ciphertext += (char)strtol(hex, NULL, 16);
		}
		std::string strVersionDatUrl = DecryptUrl(ciphertext);

		// 下载更新文件 新地址 strVersionDatUrl HOST也在这里面 /Version.dat
		// 看看 strVersionDatUrl 中有没有端口,没有就默认80
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
			// 这是一个以上结构的JSON内容
			Json::Value root;
			Json::Reader reader;
			if (reader.parse(strVersionDatContent, root)) {
				// 获取远程的MD5
				std::string strRemoteVersionDatMd5 = FileHash::string_md5(res2->body);
				// 获取版本号
				if (strRemoteVersionDatMd5 != strLocalVersionDatMd5)
				{
					// 保存到本地
					std::ofstream ofs("Version.dat", std::ios::binary);
					ofs.write(res2->body.data(), res2->body.size());
					ofs.close();

					m_qwVersion = root["time"].asInt64();
					m_mapFiles.clear();
					// 获取文件信息
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

					// 获取 runtime 文件列表
					Json::Value downloadList = root["runtime"];
					for (auto& download : downloadList) {
						m_vecRunTimeList.push_back(download.asString());
					}
				}
			}
		}
		std::cout << "cccccccccccccccccccccccccc" << std::endl;
		// 下载RunTime文件
		DownloadRunTimeFile(m_strHost,wPort);
	} 

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	unsigned long long dwTick = GetTickCount64();


	// 把MD5写到MAP中
	WriteDataToMapping();

	unsigned long long dwNewTick = GetTickCount64();
	std::cout << "WriteDataToMapping 花费时间:" << dwNewTick - dwTick << std::endl;
	dwTick = dwNewTick;

	// 启动游戏
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	if (!CreateProcess(L"MapleReborn.exe", NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		HandleError("CreateProcess failed");
	}

	// Create进程花费时间
	dwNewTick = GetTickCount64();
	std::cout << "CreateProcess 花费时间:" << dwNewTick - dwTick << std::endl;

	m_hGameProcess = pi.hProcess;

	// 监听网络请求
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
				// 下载
				httplib::Client cli(m_strHost, m_wPort);
				strPage = "/Update/" + std::to_string(it->second.m_qwTime) + "/" + strPage;
				std::replace(strPage.begin(), strPage.end(), '\\', '/');
				auto ret = cli.Get(strPage);
				if (ret && ret->status == 200)
				{
					// std::filesystem::create_directories(strLocalFile);
					// 提取目录
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

			// 返回一个OK就行 不需要json
			res.status = 200;
			res.set_content("OK", "text/plain");
			std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		}
		else {
			std::cout << "没有这个文件" << strPage <<std::endl;
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

	// 获取加密提供程序句柄
	if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		HandleError("CryptAcquireContext failed");
		return ciphertext;
	}

	// 创建哈希对象
	if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
		HandleError("CryptCreateHash failed");
		return ciphertext;
	}

	// 哈希密钥
	const char* key = "cDds!ErF9sIe6u$B";
	if (!CryptHashData(hHash, (BYTE*)key, strlen(key), 0)) {
		HandleError("CryptHashData failed");
		return ciphertext;
	}

	// 派生 AES 密钥
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
	// 对应以上加密过程来解密 字
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
		// 先查看本地是否存在这个文件
		std::string strLocalFile = download;
		std::string strPage = "/Update/" + std::to_string(m_mapFiles[download].m_qwTime) + "/" + download;
		std::replace(strPage.begin(), strPage.end(), '\\', '/');

		std::cout <<__FUNCTION__<<":" << strPage << std::endl;

		// 对比MD5
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
		
		std::cout << __FILE__ << ":" << __LINE__<< " 文件:" << strLocalFile << std::endl;
		// 删除本地文件
		// std::filesystem::remove(strLocalFile);
		DeleteFileA(strLocalFile.c_str());

		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		// 下载新文件
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
		// 删除 "\"
		std::replace(strMemoryName.begin(), strMemoryName.end(), '\\', '_');
		HANDLE hFileMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, config.m_strMd5.length() + 1, strMemoryName.c_str());
		if (hFileMapping)
		{
			// 把MD5写进去
			LPVOID lpBaseAddress = MapViewOfFile(hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
			if (lpBaseAddress)
			{
				memcpy(lpBaseAddress, config.m_strMd5.c_str(), config.m_strMd5.length());
				// 添加终止符
				((char*)lpBaseAddress)[config.m_strMd5.length()] = '\0';
				UnmapViewOfFile(lpBaseAddress);
			}
			m_hFileMappings.push_back(hFileMapping);
		}
	}
}