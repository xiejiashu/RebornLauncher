#include <string>
struct VersionConfig
{
	// 文件名包括Page文件夹的路径
	std::string m_strPage;
	// md5
	std::string m_strMd5;
	// 时间值 一般是 年月日时分秒 比如 20240927235959 可以用个int64_t存储
	int64_t m_qwTime;
	// 文件大小
	int64_t m_qwSize;
};