#pragma once

#include <Windows.h>

class WorkThread;

namespace workthread::runflow {

class WorkThreadRunCoordinator {
public:
	explicit WorkThreadRunCoordinator(WorkThread& worker);
	DWORD Execute();

private:
	bool FailWithMessage(const wchar_t* message) const;

	WorkThread& m_worker;
};

} // namespace workthread::runflow
