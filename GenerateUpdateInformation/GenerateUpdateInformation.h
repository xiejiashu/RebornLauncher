#include <vector>
#include <string>
#include <map>
class Application
{
public:
	Application(HINSTANCE hInstance, LPWSTR lpCmdLine);
	~Application();
public:
	HINSTANCE AppInstance() const;
	// 获取目录下所有文件夹
	std::vector<std::wstring> GetSubDirectories(const std::wstring& path);
	// 获取目录下所有子目录包括子目录的目录
	std::vector<std::wstring> GetSubDirectoriesRecursive(const std::wstring& path);
	// 获取目录下所有文件名全路径,包括子目录以及子目录的目录的文件
	std::vector<std::wstring> GetFilesRecursive(const std::wstring& path);
private:
	HINSTANCE m_hInstance{ nullptr };
	std::map<std::wstring, VersionConfig> m_mapFiles;
};