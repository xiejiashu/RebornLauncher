#pragma once

#include <string>

class FileHash {
public:
	static std::string string_md5(std::string const& str);
	static std::string file_md5(std::string const& filename);
};
