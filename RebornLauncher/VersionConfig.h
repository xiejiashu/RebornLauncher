#include <string>
struct VersionConfig
{
	// �ļ�������Page�ļ��е�·��
	std::string m_strPage;
	// md5
	std::string m_strMd5;
	// ʱ��ֵ һ���� ������ʱ���� ���� 20240927235959 �����ø�int64_t�洢
	int64_t m_qwTime;
	// �ļ���С
	int64_t m_qwSize;
};