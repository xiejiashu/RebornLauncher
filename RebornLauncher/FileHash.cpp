#include "FileHash.h"

#include <cryptopp/md5.h>
#include <cryptopp/hex.h>
#include <cryptopp/files.h>


//std::string FileHash::string_md5(std::wstring const& str)
//{
//	CryptoPP::MD5 md5;
//	std::string dst;
//
//	auto filter = new CryptoPP::HashFilter(md5, new CryptoPP::HexEncoder(new CryptoPP::StringSink(dst)));
//	// auto string_source = CryptoPP::StringSource(str, true, filter);
//	
//	return dst;
//}

std::string FileHash::file_md5(std::string const& filename)
{
	CryptoPP::MD5 md5;
	std::string dst;

	auto filter = new CryptoPP::HashFilter(md5, new CryptoPP::HexEncoder(new CryptoPP::StringSink(dst)));
	CryptoPP::FileSource _(filename.c_str(), true, filter);

	return dst;
}