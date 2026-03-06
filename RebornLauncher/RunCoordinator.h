#pragma once

#include <Windows.h>

class LauncherUpdateCoordinator;

namespace workthread::runflow {

class RunCoordinator {
public:
	explicit RunCoordinator(LauncherUpdateCoordinator& worker);
	DWORD Execute();

private:
	bool FailWithMessage(const wchar_t* message) const;

	LauncherUpdateCoordinator& m_worker;
};

} // namespace workthread::runflow
