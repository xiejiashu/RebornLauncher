#include <string>
struct VersionConfig
{
	// 鏂囦欢鍚嶅寘鎷琍age鏂囦欢澶圭殑璺緞
	std::string m_strPage;
	// md5
	std::string m_strMd5;
	// 鏃堕棿鍊?涓€鑸槸 骞存湀鏃ユ椂鍒嗙 姣斿 20240927235959 鍙互鐢ㄤ釜int64_t瀛樺偍
	int64_t m_qwTime;
	// 鏂囦欢澶у皬
	int64_t m_qwSize;
};