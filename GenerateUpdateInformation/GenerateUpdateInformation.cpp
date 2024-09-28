#include "framework.h"
#include "VersionConfig.h"
#include <zstd.h>
#include <fstream>
#include <json/json.h>
#include <iostream>
#include "FileHash.h"
#include "GenerateUpdateInformation.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

	// ����һ������̨
	AllocConsole();
	FILE* fp = nullptr;
	freopen_s(&fp, "CONOUT$", "w", stdout);

	Application app(hInstance,lpCmdLine);
	return 0;
}

std::string wstr2str(const std::wstring& wstr) {
	std::string result;
	//��ȡ��������С��������ռ䣬��������С�°��ֽڼ����  
	int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), NULL, 0, NULL, NULL);
	char* buffer = new char[len + 1];
	if (!buffer)
		return "";

	memset(buffer, 0, len + 1);
	//���ֽڱ���ת���ɶ��ֽڱ���  
	WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), buffer, len, NULL, NULL);
	buffer[len] = '\0';
	//ɾ��������������ֵ  
	result.append(buffer);
	delete[] buffer;
	return result;
}

std::wstring str2wstr(const std::string& str, int strLen) {
	int hLen = strLen;
	if (strLen < 0)
		hLen = str.length();
	std::wstring result;
	//��ȡ��������С��������ռ䣬��������С���ַ�����  
	int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), hLen, NULL, 0);
	WCHAR* buffer = new WCHAR[len + 1];
	if (!buffer)
		return L"";
	//���ֽڱ���ת���ɿ��ֽڱ���  
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), hLen, buffer, len);
	buffer[len] = '\0';             //����ַ�����β  
	//ɾ��������������ֵ  
	result.append(buffer);
	delete[] buffer;
	return result;
}

Application::Application(HINSTANCE hInstance, LPWSTR lpCmdLine)
	: m_hInstance(hInstance)
{
	std::cout << "��ʼ��ȡ�ļ�..." << std::endl;
	// ��ȡ��ǰĿ¼
	TCHAR szPath[MAX_PATH] = { 0 };
	GetCurrentDirectory(MAX_PATH, szPath);
	// auto dirs = GetSubDirectories(szPath);
	std::wstring strPath = szPath;
	strPath.append(TEXT("\\Update"));
	auto files = GetFilesRecursive(strPath);

	// ���ļ�����strPath��ʼ��ȡ �ļ��������� C:\\AAA\\Update\\2024025959\\..Page..\\1.txt
	std::cout << "��ʼ�����ļ���Ϣ..." << std::endl;
	int64_t qwTime = 0;
	for (auto& file : files)
	{
		std::cout << wstr2str(file) << std::endl;
		std::wstring strFileName = file.substr(strPath.length() + 1);
		VersionConfig config;
		// ��ʱ�����ȡ����
		size_t nPos = strFileName.find_first_of(TEXT("\\"));
		std::wstring strTime = strFileName.substr(0, nPos);
		config.m_qwTime = _wtoi64(strTime.c_str());
		// ���ļ�����ȡ����
		strFileName = strFileName.substr(nPos + 1);

		if (config.m_qwTime > qwTime)
		{
			qwTime = config.m_qwTime;
		}

		// �����ļ������ǲ�����ͬ�����ļ� ���û�У�����ʱ��������ڵľɣ��͸���
		auto iter = m_mapFiles.find(strFileName);
		if (iter == m_mapFiles.end() || iter->second.m_qwTime < config.m_qwTime)
		{
			// ����file�ļ���С
			WIN32_FILE_ATTRIBUTE_DATA fileAttr;
			GetFileAttributesEx(file.c_str(), GetFileExInfoStandard, &fileAttr);
			config.m_qwSize = fileAttr.nFileSizeLow | ((((int64_t)fileAttr.nFileSizeHigh) & 0xFFFFFFFF) << 32);
			// config.m_strPage = strFileName;
			
			// ����ת����
			config.m_strPage = wstr2str(strFileName);

			// ����file�ļ���md5
			config.m_strMd5 = FileHash::file_md5(file);
			m_mapFiles[strFileName] = config;
		}
	}

	// ת��JSON�ṹ
	Json::Value root;
	// д��ʱ���
	root["time"] = qwTime;
	// д���ļ���Ϣ
	Json::Value filesJson;
	for (auto& [fileName,config] : m_mapFiles)
	{
		Json::Value fileJson;
		fileJson["md5"] = config.m_strMd5;
		fileJson["time"] = config.m_qwTime;
		fileJson["size"] = config.m_qwSize;
		fileJson["page"] = config.m_strPage;
		root["file"].append(fileJson);
	}

#ifndef _DEBUG
	std::cout << "����ѹ����Ϣ..." << std::endl;
	// ��ZSDT ����ѹ�� ������ ��ԿΪ "KRu998Am"
	std::string strJson = root.toStyledString();
	std::string strCompress;
	ZSTD_CCtx* cctx = ZSTD_createCCtx();
	size_t compressBound = ZSTD_compressBound(strJson.length());
	strCompress.resize(compressBound);
	size_t compressSize = ZSTD_compressCCtx(cctx, &strCompress[0], compressBound, strJson.c_str(), strJson.length(), 3);
	strCompress.resize(compressSize);
	ZSTD_freeCCtx(cctx);

	std::ofstream ofs("Version.dat", std::ios::binary);
	ofs.write(strCompress.c_str(), strCompress.length());
	ofs.close();
#else
	// ���浽�ļ�
	std::ofstream ofs("Version.dat");
	ofs << root.toStyledString();
	ofs.close();
#endif

	std::cout << "���!" << std::endl;
}

Application::~Application()
{

}

HINSTANCE Application::AppInstance() const
{
    return m_hInstance;
}

std::vector<std::wstring> Application::GetSubDirectories(const std::wstring& path)
{
	std::vector<std::wstring> subDirectories;
	WIN32_FIND_DATA findFileData;
	HANDLE hFind = FindFirstFile((path + TEXT("\\*")).c_str(), &findFileData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		return subDirectories;
	}
	do
	{
		if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (wcscmp(findFileData.cFileName, TEXT(".")) != 0 && wcscmp(findFileData.cFileName, TEXT("..")) != 0)
			{
				subDirectories.push_back(findFileData.cFileName);
			}
		}
	} while (FindNextFile(hFind, &findFileData));
	FindClose(hFind);
	return subDirectories;
}

std::vector<std::wstring> Application::GetSubDirectoriesRecursive(const std::wstring& path)
{
	std::vector<std::wstring> subDirectories;
	WIN32_FIND_DATA findFileData;
	HANDLE hFind = FindFirstFile((path + TEXT("\\*")).c_str(), &findFileData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		return subDirectories;
	}
	do
	{
		if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (wcscmp(findFileData.cFileName, TEXT(".")) != 0 && wcscmp(findFileData.cFileName, TEXT("..")) != 0)
			{
				subDirectories.push_back(findFileData.cFileName);
				auto subSubDirectories = GetSubDirectoriesRecursive(path + TEXT("\\") + findFileData.cFileName);
				subDirectories.insert(subDirectories.end(), subSubDirectories.begin(), subSubDirectories.end());
			}
		}
	} while (FindNextFile(hFind, &findFileData));
	FindClose(hFind);
	return subDirectories;
}

std::vector<std::wstring> Application::GetFilesRecursive(const std::wstring& path)
{
	std::vector<std::wstring> files;
	WIN32_FIND_DATA findFileData;
	HANDLE hFind = FindFirstFile((path + TEXT("\\*")).c_str(), &findFileData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		return files;
	}
	do
	{
		if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			files.push_back(path + TEXT("\\") + findFileData.cFileName);
		}
		else
		{
			if (wcscmp(findFileData.cFileName, TEXT(".")) != 0 && wcscmp(findFileData.cFileName, TEXT("..")) != 0)
			{
				auto subFiles = GetFilesRecursive(path + TEXT("\\") + findFileData.cFileName);
				files.insert(files.end(), subFiles.begin(), subFiles.end());
			}
		}
	} while (FindNextFile(hFind, &findFileData));
	FindClose(hFind);
	return files;
}