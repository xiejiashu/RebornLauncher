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
	// ��ȡĿ¼�������ļ���
	std::vector<std::wstring> GetSubDirectories(const std::wstring& path);
	// ��ȡĿ¼��������Ŀ¼������Ŀ¼��Ŀ¼
	std::vector<std::wstring> GetSubDirectoriesRecursive(const std::wstring& path);
	// ��ȡĿ¼�������ļ���ȫ·��,������Ŀ¼�Լ���Ŀ¼��Ŀ¼���ļ�
	std::vector<std::wstring> GetFilesRecursive(const std::wstring& path);
private:
	HINSTANCE m_hInstance{ nullptr };
	std::map<std::wstring, VersionConfig> m_mapFiles;
};