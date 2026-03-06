#pragma once

class LauncherUpdateCoordinator;

namespace workthread::runtimeupdate {

class RuntimeUpdater {
public:
	explicit RuntimeUpdater(LauncherUpdateCoordinator& worker);
	bool Execute();

private:
	LauncherUpdateCoordinator& m_worker;
};

} // namespace workthread::runtimeupdate
