#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include <archive.h>
#include <archive_entry.h>

namespace {

std::string NormalizeArchivePath(std::string path) {
	std::replace(path.begin(), path.end(), '\\', '/');
	while (!path.empty() && (path.front() == '/' || path.front() == '.')) {
		if (path.front() == '.') {
			if (path.size() >= 2 && path[1] == '/') {
				path.erase(path.begin(), path.begin() + 2);
				continue;
			}
			break;
		}
		path.erase(path.begin());
	}
	return path;
}

std::string GetArchiveParentPath(const std::string& path) {
	const size_t pos = path.find_last_of('/');
	if (pos == std::string::npos) {
		return {};
	}
	return path.substr(0, pos);
}

std::string GetArchiveFileName(const std::string& path) {
	const size_t pos = path.find_last_of('/');
	if (pos == std::string::npos) {
		return path;
	}
	return path.substr(pos + 1);
}

std::vector<std::string> SplitArchivePath(const std::string& path) {
	std::vector<std::string> parts;
	size_t start = 0;
	while (start < path.size()) {
		size_t slash = path.find('/', start);
		if (slash == std::string::npos) {
			slash = path.size();
		}
		if (slash > start) {
			parts.push_back(path.substr(start, slash - start));
		}
		start = slash + 1;
	}
	return parts;
}

std::string JoinArchivePath(const std::vector<std::string>& parts) {
	if (parts.empty()) {
		return {};
	}
	std::string out = parts.front();
	for (size_t i = 1; i < parts.size(); ++i) {
		out.push_back('/');
		out.append(parts[i]);
	}
	return out;
}

std::string GetLowerAscii(std::string value) {
	for (auto& c : value) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return value;
}

std::string DetermineExeRootPrefix(const std::vector<DataBlock>& files) {
	std::vector<std::vector<std::string>> exeParentParts;
	for (const auto& file : files) {
		const std::string fileName = GetArchiveFileName(file.filePath);
		const std::string lowerName = GetLowerAscii(fileName);
		if (lowerName.size() < 4 || lowerName.substr(lowerName.size() - 4) != ".exe") {
			continue;
		}

		const std::string parent = GetArchiveParentPath(file.filePath);
		if (parent.empty()) {
			return {};
		}
		exeParentParts.push_back(SplitArchivePath(parent));
	}

	if (exeParentParts.empty()) {
		return {};
	}

	std::vector<std::string> common = exeParentParts.front();
	for (size_t i = 1; i < exeParentParts.size(); ++i) {
		const auto& parts = exeParentParts[i];
		size_t commonLen = 0;
		while (commonLen < common.size() && commonLen < parts.size() && common[commonLen] == parts[commonLen]) {
			++commonLen;
		}
		common.resize(commonLen);
		if (common.empty()) {
			break;
		}
	}

	return JoinArchivePath(common);
}

std::string StripArchivePrefix(const std::string& fullPath, const std::string& prefix) {
	if (prefix.empty()) {
		return fullPath;
	}
	if (fullPath == prefix) {
		return {};
	}
	if (fullPath.size() > prefix.size() &&
		fullPath.compare(0, prefix.size(), prefix) == 0 &&
		fullPath[prefix.size()] == '/') {
		return fullPath.substr(prefix.size() + 1);
	}
	return fullPath;
}

bool MakeSafeRelativePath(const std::string& input, std::string& out) {
	std::filesystem::path p(input);
	p = p.lexically_normal();
	if (p.empty() || p.is_absolute()) {
		return false;
	}

	for (const auto& part : p) {
		const std::string token = part.string();
		if (token.empty() || token == ".") {
			continue;
		}
		if (token == "..") {
			return false;
		}
	}

	out = p.generic_string();
	while (!out.empty() && out.front() == '/') {
		out.erase(out.begin());
	}
	return !out.empty();
}

} // namespace

void WorkThread::Extract7z(const std::string& filename, const std::string& destPath)
{
	std::vector<DataBlock> allFiles = ScanArchive(filename);

	m_nCurrentDownloadSize = static_cast<int>(allFiles.size());
	m_nCurrentDownloadProgress = 0;
	if (m_nCurrentDownloadSize > 0)
	{
		const unsigned int hwThreads = std::thread::hardware_concurrency();
		const size_t preferredThreads = hwThreads == 0 ? 1 : static_cast<size_t>(hwThreads);
		const size_t numThreads = (std::min)(preferredThreads, allFiles.size());
		const size_t filesPerThread = (allFiles.size() + numThreads - 1) / numThreads;

		std::vector<std::thread> threads;
		for (size_t i = 0; i < numThreads; ++i) {
			size_t start = i * filesPerThread;
			size_t end = (i == numThreads - 1) ? allFiles.size() : (i + 1) * filesPerThread;
			if (start >= end) {
				continue;
			}
			std::vector<DataBlock> files(allFiles.begin() + start, allFiles.begin() + end);
			threads.emplace_back(&WorkThread::ExtractFiles, this, filename, destPath, files);
		}

		for (auto& t : threads) {
			if (t.joinable()) {
				t.join();
			}
		}

		std::cout << filename << " extraction completed" << std::endl;
	}
	else
	{
		std::cout << filename << " is empty, no files to extract" << std::endl;
	}
}

std::vector<DataBlock> WorkThread::ScanArchive(const std::string& archivePath) {
	struct archive* a;
	struct archive_entry* entry;
	std::vector<DataBlock> files;

	a = archive_read_new();
	archive_read_support_format_7zip(a);
	archive_read_support_filter_all(a);

	if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
		std::cerr << "Failed to open archive: " << archive_error_string(a) << std::endl;
		archive_read_free(a);
		return files;
	}

	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		const char* entryPath = archive_entry_pathname(entry);
		if (entryPath == nullptr || entryPath[0] == '\0') {
			archive_read_data_skip(a);
			continue;
		}
		if (archive_entry_filetype(entry) != AE_IFREG) {
			archive_read_data_skip(a);
			continue;
		}

		DataBlock fileInfo;
		fileInfo.filePath = NormalizeArchivePath(entryPath);
		if (fileInfo.filePath.empty()) {
			archive_read_data_skip(a);
			continue;
		}
		fileInfo.fileOffset = archive_read_header_position(a);
		files.push_back(fileInfo);
		archive_read_data_skip(a);
	}

	archive_read_close(a);
	archive_read_free(a);

	const std::string extractRootPrefix = DetermineExeRootPrefix(files);

	if (!extractRootPrefix.empty()) {
		m_extractRootPrefix = extractRootPrefix;
	}
	else {
		m_extractRootPrefix.clear();
	}

	return files;
}

void WorkThread::ExtractFiles(const std::string& archivePath, const std::string& outPath, const std::vector<DataBlock>& files) {
	struct archive* a;
	struct archive_entry* entry;
	const std::string extractRootPrefix = m_extractRootPrefix;
	a = archive_read_new();
	archive_read_support_format_7zip(a);
	archive_read_support_filter_all(a);
	do {
		if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
			std::cerr << "Failed to reopen archive for file: " << std::endl;
			break;
		}

		for (const auto& fileInfo : files) {
			while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
				const char* entryPath = archive_entry_pathname(entry);
				if (entryPath == nullptr || entryPath[0] == '\0') {
					archive_read_data_skip(a);
					continue;
				}
				if (archive_entry_filetype(entry) != AE_IFREG) {
					archive_read_data_skip(a);
					continue;
				}
				const std::string normalizedEntryPath = NormalizeArchivePath(entryPath);
				if (fileInfo.filePath == normalizedEntryPath) {
					std::string relativePath = StripArchivePrefix(normalizedEntryPath, extractRootPrefix);
					relativePath = NormalizeArchivePath(relativePath);
					if (relativePath.empty()) {
						relativePath = GetArchiveFileName(normalizedEntryPath);
					}

					std::string safeRelativePath;
					if (!MakeSafeRelativePath(relativePath, safeRelativePath)) {
						archive_read_data_skip(a);
						break;
					}

					std::string outputPath = outPath + "/" + safeRelativePath;
					std::filesystem::path filePath = outputPath;
					if (!std::filesystem::exists(filePath.parent_path()))
					{
						std::filesystem::create_directories(filePath.parent_path());
					}
					std::ofstream outputFile(outputPath, std::ios::binary);
					if (!outputFile.is_open()) {
						std::cerr << "Failed to create output file: " << outputPath << std::endl;
						break;
					}

					const void* buffer;
					size_t size;
					la_int64_t offset;
					while (archive_read_data_block(a, &buffer, &size, &offset) == ARCHIVE_OK) {
						outputFile.write((const char*)buffer, static_cast<std::streamsize>(size));
					}

					outputFile.close();
					{
						m_nCurrentDownloadProgress++;
					}
					break;
				}
			}
		}
	} while (false);

	archive_read_close(a);
	archive_read_free(a);
}
