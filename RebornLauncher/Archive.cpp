#include "framework.h"
#include "LauncherUpdateCoordinator.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
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

bool VerifyArchiveReadableInternal(const std::string& archivePath, std::string& errorOut) {
	struct archive* a = archive_read_new();
	archive_read_support_format_7zip(a);
	archive_read_support_filter_all(a);

	if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
		errorOut = archive_error_string(a) ? archive_error_string(a) : "open archive failed";
		archive_read_free(a);
		return false;
	}

	struct archive_entry* entry = nullptr;
	bool hasRegularEntry = false;
	int headerResult = ARCHIVE_OK;
	while ((headerResult = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
		if (archive_entry_filetype(entry) != AE_IFREG) {
			archive_read_data_skip(a);
			continue;
		}

		hasRegularEntry = true;
		const void* buffer = nullptr;
		size_t size = 0;
		la_int64_t offset = 0;
		int dataResult = ARCHIVE_OK;
		while ((dataResult = archive_read_data_block(a, &buffer, &size, &offset)) == ARCHIVE_OK) {
		}
		if (dataResult != ARCHIVE_EOF) {
			errorOut = archive_error_string(a) ? archive_error_string(a) : "archive data block read failed";
			archive_read_close(a);
			archive_read_free(a);
			return false;
		}
	}

	if (headerResult != ARCHIVE_EOF) {
		errorOut = archive_error_string(a) ? archive_error_string(a) : "archive header read failed";
		archive_read_close(a);
		archive_read_free(a);
		return false;
	}

	archive_read_close(a);
	archive_read_free(a);
	if (!hasRegularEntry) {
		errorOut = "archive contains no regular files";
		return false;
	}
	return true;
}

} // namespace

bool LauncherUpdateCoordinator::VerifyArchiveReadable(const std::string& archivePath) {
	std::string error;
	if (VerifyArchiveReadableInternal(archivePath, error)) {
		return true;
	}
	LogUpdateErrorDetailsFmt(
		"UF-ARCHIVE-VERIFY",
		"LauncherUpdateCoordinator::VerifyArchiveReadable",
		"Archive verification failed",
		"archive={}, error={}",
		archivePath,
		error);
	return false;
}

bool LauncherUpdateCoordinator::Extract7z(const std::string& filename, const std::string& destPath)
{
	std::vector<DataBlock> allFiles = ScanArchive(filename);
	if (allFiles.empty()) {
		LogUpdateErrorDetailsFmt(
			"UF-ARCHIVE-EMPTY",
			"LauncherUpdateCoordinator::Extract7z",
			"Archive has no extractable files",
			"archive={}",
			filename);
		return false;
	}

	m_downloadState.currentDownloadSize = static_cast<int>(allFiles.size());
	m_downloadState.currentDownloadProgress = 0;
	if (m_downloadState.currentDownloadSize > 0)
	{
		const unsigned int hwThreads = std::thread::hardware_concurrency();
		const size_t preferredThreads = hwThreads == 0 ? 1 : static_cast<size_t>(hwThreads);
		const size_t numThreads = (std::min)(preferredThreads, allFiles.size());
		const size_t filesPerThread = (allFiles.size() + numThreads - 1) / numThreads;

		std::atomic<bool> extractFailed{ false };
		std::vector<std::thread> threads;
		for (size_t i = 0; i < numThreads; ++i) {
			size_t start = i * filesPerThread;
			size_t end = (i == numThreads - 1) ? allFiles.size() : (i + 1) * filesPerThread;
			if (start >= end) {
				continue;
			}
			std::vector<DataBlock> filesBatch(allFiles.begin() + start, allFiles.begin() + end);
			threads.emplace_back([this, filename, destPath, batch = std::move(filesBatch), &extractFailed]() {
				if (!ExtractFiles(filename, destPath, batch)) {
					extractFailed.store(true);
				}
			});
		}

		for (auto& t : threads) {
			if (t.joinable()) {
				t.join();
			}
		}

		if (extractFailed.load()) {
			LogUpdateErrorDetailsFmt(
				"UF-ARCHIVE-EXTRACT",
				"LauncherUpdateCoordinator::Extract7z",
				"Archive extraction failed",
				"archive={}",
				filename);
			return false;
		}

		LogUpdateInfoFmt(
			"UF-ARCHIVE-EXTRACT",
			"LauncherUpdateCoordinator::Extract7z",
			"Archive extraction completed (archive={})",
			filename);
		return true;
	}

	LogUpdateWarnFmt(
		"UF-ARCHIVE-EMPTY",
		"LauncherUpdateCoordinator::Extract7z",
		"Archive contains no files to extract (archive={})",
		filename);
	return false;
}

std::vector<DataBlock> LauncherUpdateCoordinator::ScanArchive(const std::string& archivePath) {
	struct archive* a;
	struct archive_entry* entry;
	std::vector<DataBlock> files;

	a = archive_read_new();
	archive_read_support_format_7zip(a);
	archive_read_support_filter_all(a);

	if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
		LogUpdateErrorDetailsFmt(
			"UF-ARCHIVE-OPEN",
			"LauncherUpdateCoordinator::ScanArchive",
			"Failed to open archive",
			"archive={}, error={}",
			archivePath,
			archive_error_string(a));
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
		m_versionState.extractRootPrefix = extractRootPrefix;
	}
	else {
		m_versionState.extractRootPrefix.clear();
	}

	return files;
}

bool LauncherUpdateCoordinator::ExtractFiles(const std::string& archivePath, const std::string& outPath, const std::vector<DataBlock>& files) {
	struct archive* a;
	struct archive_entry* entry;
	const std::string extractRootPrefix = m_versionState.extractRootPrefix;
	a = archive_read_new();
	archive_read_support_format_7zip(a);
	archive_read_support_filter_all(a);
	if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
		LogUpdateErrorDetailsFmt(
			"UF-ARCHIVE-REOPEN",
			"LauncherUpdateCoordinator::ExtractFiles",
			"Failed to reopen archive for extraction",
			"archive={}, error={}",
			archivePath,
			archive_error_string(a));
		archive_read_free(a);
		return false;
	}

	for (const auto& fileInfo : files) {
		bool extracted = false;
		int headerResult = ARCHIVE_OK;
		while ((headerResult = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
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
			if (fileInfo.filePath != normalizedEntryPath) {
				archive_read_data_skip(a);
				continue;
			}

			std::string relativePath = StripArchivePrefix(normalizedEntryPath, extractRootPrefix);
			relativePath = NormalizeArchivePath(relativePath);
			if (relativePath.empty()) {
				relativePath = GetArchiveFileName(normalizedEntryPath);
			}

			std::string safeRelativePath;
			if (!MakeSafeRelativePath(relativePath, safeRelativePath)) {
				archive_read_close(a);
				archive_read_free(a);
				return false;
			}

			std::string outputPath = outPath + "/" + safeRelativePath;
			std::filesystem::path filePath = std::filesystem::u8path(outputPath);
			std::error_code ec;
			const auto parent = filePath.parent_path();
			if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
				std::filesystem::create_directories(parent, ec);
				if (ec) {
					archive_read_close(a);
					archive_read_free(a);
					return false;
				}
			}

			std::ofstream outputFile(filePath, std::ios::binary | std::ios::trunc);
			if (!outputFile.is_open()) {
				LogUpdateErrorDetailsFmt(
					"UF-ARCHIVE-WRITE",
					"LauncherUpdateCoordinator::ExtractFiles",
					"Failed to create output file",
					"archive={}, output={}",
					archivePath,
					outputPath);
				archive_read_close(a);
				archive_read_free(a);
				return false;
			}

			const void* buffer = nullptr;
			size_t size = 0;
			la_int64_t offset = 0;
			int dataResult = ARCHIVE_OK;
			while ((dataResult = archive_read_data_block(a, &buffer, &size, &offset)) == ARCHIVE_OK) {
				outputFile.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
				if (!outputFile.good()) {
					outputFile.close();
					archive_read_close(a);
					archive_read_free(a);
					return false;
				}
			}

			outputFile.close();
			if (!outputFile.good() || dataResult != ARCHIVE_EOF) {
				archive_read_close(a);
				archive_read_free(a);
				return false;
			}

			m_downloadState.currentDownloadProgress++;
			extracted = true;
			break;
		}

		if (!extracted || headerResult != ARCHIVE_OK) {
			archive_read_close(a);
			archive_read_free(a);
			return false;
		}
	}

	archive_read_close(a);
	archive_read_free(a);
	return true;
}
